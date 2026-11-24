#include "smart_filter_engine.h"

#include <algorithm>
#include <cctype>

namespace Ayu::SmartFilter {
namespace {

std::string toLowerAscii(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
}

std::string stripLeadingAt(std::string s) {
	while (!s.empty() && s.front() == '@') {
		s.erase(s.begin());
	}
	return s;
}

bool containsText(const std::string &haystack, const std::string &needle, bool caseSensitive) {
	if (needle.empty() || haystack.empty()) {
		return false;
	}
	if (caseSensitive) {
		return haystack.find(needle) != std::string::npos;
	}
	return toLowerAscii(haystack).find(toLowerAscii(needle)) != std::string::npos;
}

std::string extractHostFromUrl(const std::string &url) {
	std::string lower = toLowerAscii(url);
	auto pos = lower.find("://");
	size_t start = (pos == std::string::npos) ? 0 : pos + 3;
	size_t end = lower.find_first_of("/?#: ", start);
	std::string host = (end == std::string::npos)
		? lower.substr(start)
		: lower.substr(start, end - start);
	if (host.rfind("www.", 0) == 0) {
		host.erase(0, 4);
	}
	return host;
}

bool hostMatchesDomain(const std::string &host, const std::string &domain) {
	if (host.empty() || domain.empty()) {
		return false;
	}
	if (host == domain) {
		return true;
	}
	if (host.size() > domain.size() + 1) {
		const size_t offset = host.size() - domain.size();
		return host[offset - 1] == '.' && host.compare(offset, domain.size(), domain) == 0;
	}
	return false;
}

} // namespace

SmartFilterEngine &SmartFilterEngine::instance() {
	static SmartFilterEngine engine;
	return engine;
}

void SmartFilterEngine::setConfig(SmartFilterConfig config) {
	ChangeCallback cb;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_config = std::move(config);
		recompileRulesLocked();
		cb = _onChange;
	}
	if (cb) {
		cb(0);
	}
}

SmartFilterConfig SmartFilterEngine::config() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _config;
}

void SmartFilterEngine::setChatReactionFilter(int64_t chatId, bool enabled, int minReactions) {
	ChangeCallback cb;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto &cfg = _config.chatReactionConfigs[chatId];
		cfg.enabled = enabled;
		cfg.minReactions = std::max(0, minReactions);
		cb = _onChange;
	}
	if (cb) {
		cb(chatId);
	}
}

ReactionChatConfig SmartFilterEngine::chatReactionConfig(int64_t chatId) const {
	std::lock_guard<std::mutex> lock(_mutex);
	const auto it = _config.chatReactionConfigs.find(chatId);
	if (it != _config.chatReactionConfigs.end()) {
		return it->second;
	}
	return ReactionChatConfig{};
}

void SmartFilterEngine::setChatAdWhitelisted(int64_t chatId, bool whitelisted) {
	ChangeCallback cb;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (whitelisted) {
			_config.adWhitelistChatIds.insert(chatId);
		} else {
			_config.adWhitelistChatIds.erase(chatId);
		}
		cb = _onChange;
	}
	if (cb) {
		cb(chatId);
	}
}

bool SmartFilterEngine::isChatAdWhitelisted(int64_t chatId) const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _config.adWhitelistChatIds.count(chatId) > 0;
}

void SmartFilterEngine::setTemporaryRevealAll(int64_t chatId, bool reveal) {
	ChangeCallback cb;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (reveal) {
			_temporaryRevealChats.insert(chatId);
		} else {
			_temporaryRevealChats.erase(chatId);
		}
		cb = _onChange;
	}
	if (cb) {
		cb(chatId);
	}
}

bool SmartFilterEngine::isTemporaryRevealAll(int64_t chatId) const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _temporaryRevealChats.count(chatId) > 0;
}

void SmartFilterEngine::onActiveChatChanged(int64_t previousChatId, int64_t /*nextChatId*/) {
	ChangeCallback cb;
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		if (previousChatId != 0 && _temporaryRevealChats.erase(previousChatId) > 0) {
			changed = true;
			cb = _onChange;
		}
	}
	if (changed && cb) {
		cb(previousChatId);
	}
}

void SmartFilterEngine::recompileRulesLocked() {
	_compiledRules.clear();
	_compiledRules.reserve(_config.adRules.size());
	for (const auto &rule : _config.adRules) {
		if (!rule.enabled || rule.pattern.empty()) {
			continue;
		}
		CompiledRule compiled;
		compiled.rule = rule;
		if (rule.type == AdRuleType::Regex) {
			try {
				auto flags = std::regex::ECMAScript;
				if (!rule.caseSensitive) {
					flags |= std::regex::icase;
				}
				compiled.compiledRegex.emplace(rule.pattern, flags);
			} catch (const std::regex_error &) {
				continue;
			}
		} else if (rule.type == AdRuleType::Domain) {
			compiled.normalizedPattern = extractHostFromUrl(rule.pattern);
		} else if (rule.type == AdRuleType::Channel) {
			compiled.normalizedPattern = toLowerAscii(stripLeadingAt(rule.pattern));
		} else {
			compiled.normalizedPattern = rule.caseSensitive
				? rule.pattern
				: toLowerAscii(rule.pattern);
		}
		_compiledRules.push_back(std::move(compiled));
	}
}

bool SmartFilterEngine::matchSingleRule(
		const CompiledRule &compiled,
		const MessageSnapshot &msg) {
	const auto &rule = compiled.rule;
	if (rule.chatId.has_value() && *rule.chatId != msg.chatId) {
		return false;
	}

	auto scanAllFields = [&](const auto &predicate) -> bool {
		if (predicate(msg.textAndCaption)) return true;
		if (predicate(msg.forwardedFromName)) return true;
		if (predicate(msg.forwardedFromUsername)) return true;
		for (const auto &u : msg.entityUrls) {
			if (predicate(u)) return true;
		}
		for (const auto &t : msg.inlineButtonTexts) {
			if (predicate(t)) return true;
		}
		for (const auto &u : msg.inlineButtonUrls) {
			if (predicate(u)) return true;
		}
		return false;
	};

	switch (rule.type) {
	case AdRuleType::Keyword:
		return scanAllFields([&](const std::string &field) {
			return containsText(field, rule.pattern, rule.caseSensitive);
		});

	case AdRuleType::Regex:
		if (!compiled.compiledRegex.has_value()) {
			return false;
		}
		return scanAllFields([&](const std::string &field) {
			return !field.empty() && std::regex_search(field, *compiled.compiledRegex);
		});

	case AdRuleType::Domain: {
		const auto &targetDomain = compiled.normalizedPattern;
		for (const auto &u : msg.entityUrls) {
			if (hostMatchesDomain(extractHostFromUrl(u), targetDomain)) return true;
		}
		for (const auto &u : msg.inlineButtonUrls) {
			if (hostMatchesDomain(extractHostFromUrl(u), targetDomain)) return true;
		}
		return containsText(msg.textAndCaption, targetDomain, false);
	}

	case AdRuleType::Channel: {
		const auto &targetChannel = compiled.normalizedPattern;
		if (targetChannel.empty()) return false;
		const auto fwdUser = toLowerAscii(stripLeadingAt(msg.forwardedFromUsername));
		if (fwdUser == targetChannel) return true;
		if (containsText(msg.forwardedFromName, targetChannel, false)) return true;
		const std::string atHandle = "@" + targetChannel;
		const std::string tmeLink = "t.me/" + targetChannel;
		return scanAllFields([&](const std::string &field) {
			return containsText(field, atHandle, false) || containsText(field, tmeLink, false);
		});
	}
	}
	return false;
}

bool SmartFilterEngine::matchesAdRulesLocked(const MessageSnapshot &msg) const {
	for (const auto &compiled : _compiledRules) {
		if (matchSingleRule(compiled, msg)) {
			return true;
		}
	}
	return false;
}

FilterReason SmartFilterEngine::evaluate(
		const MessageSnapshot &msg,
		bool isSearchContext) const {
	std::lock_guard<std::mutex> lock(_mutex);

	if (_temporaryRevealChats.count(msg.chatId) > 0) {
		return FilterReason::None;
	}

	// 1. Ad filter has highest priority (runs even in Ctrl+F search) unless whitelisted.
	const bool adWhitelisted = _config.adWhitelistChatIds.count(msg.chatId) > 0;
	if (_config.adFilterEnabled && !adWhitelisted) {
		if (_config.hideSponsoredMessages && msg.isSponsored) {
			return FilterReason::SponsoredAd;
		}
		if (matchesAdRulesLocked(msg)) {
			return FilterReason::CustomAdRule;
		}
	}

	// 2. Ctrl+F in-chat search bypasses reaction threshold filtering.
	if (isSearchContext) {
		return FilterReason::None;
	}

	// 3. Per-chat reaction count filter.
	const auto it = _config.chatReactionConfigs.find(msg.chatId);
	if (it == _config.chatReactionConfigs.end() || !it->second.enabled) {
		return FilterReason::None;
	}
	const auto &rcfg = it->second;

	if (msg.isPinned && rcfg.exemptPinned) {
		return FilterReason::None;
	}
	if (msg.isService && rcfg.hideServiceMessages) {
		return FilterReason::ServiceMessage;
	}
	if (msg.totalReactions < rcfg.minReactions) {
		return FilterReason::LowReactions;
	}
	return FilterReason::None;
}

void SmartFilterEngine::recordEvaluation(
		int64_t chatId,
		int64_t messageId,
		FilterReason reason) {
	std::lock_guard<std::mutex> lock(_mutex);
	auto &map = _recordedReasons[chatId];
	if (reason == FilterReason::None) {
		map.erase(messageId);
	} else {
		map[messageId] = reason;
	}
}

void SmartFilterEngine::clearChatStats(int64_t chatId) {
	std::lock_guard<std::mutex> lock(_mutex);
	_recordedReasons.erase(chatId);
}

ChatHiddenStats SmartFilterEngine::chatStats(int64_t chatId) const {
	std::lock_guard<std::mutex> lock(_mutex);
	ChatHiddenStats stats;
	const auto it = _recordedReasons.find(chatId);
	if (it == _recordedReasons.end()) {
		return stats;
	}
	for (const auto &[_, reason] : it->second) {
		switch (reason) {
		case FilterReason::SponsoredAd:
		case FilterReason::CustomAdRule:
			++stats.adHidden;
			break;
		case FilterReason::LowReactions:
			++stats.lowReactionHidden;
			break;
		case FilterReason::ServiceMessage:
			++stats.serviceHidden;
			break;
		case FilterReason::None:
			break;
		}
	}
	return stats;
}

void SmartFilterEngine::setOnChangeCallback(ChangeCallback cb) {
	std::lock_guard<std::mutex> lock(_mutex);
	_onChange = std::move(cb);
}

void SmartFilterEngine::notifyDynamicReactionChange(int64_t chatId, int64_t /*messageId*/) {
	ChangeCallback cb;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		cb = _onChange;
	}
	if (cb) {
		cb(chatId);
	}
}

} // namespace Ayu::SmartFilter
