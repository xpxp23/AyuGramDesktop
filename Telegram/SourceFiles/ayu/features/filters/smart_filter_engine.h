#pragma once

#include "smart_filter_config.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ayu::SmartFilter {

enum class FilterReason {
	None,
	SponsoredAd,
	CustomAdRule,
	LowReactions,
	ServiceMessage,
};

struct MessageSnapshot {
	int64_t chatId = 0;
	int64_t messageId = 0;
	uint64_t mediaGroupId = 0; // 0 if not part of an album
	bool isSponsored = false;
	bool isPinned = false;
	bool isService = false;

	// Total reactions across all types (standard, custom, paid Stars, positive & negative).
	// For media groups (albums), set this to the aggregated album reaction total.
	int totalReactions = 0;

	// Fields scanned by ad rules:
	std::string textAndCaption;
	std::vector<std::string> entityUrls;
	std::vector<std::string> inlineButtonTexts;
	std::vector<std::string> inlineButtonUrls;
	std::string forwardedFromName;
	std::string forwardedFromUsername;
};

struct ChatHiddenStats {
	int adHidden = 0;
	int lowReactionHidden = 0;
	int serviceHidden = 0;

	[[nodiscard]] int total() const {
		return adHidden + lowReactionHidden + serviceHidden;
	}
};

class SmartFilterEngine {
public:
	static SmartFilterEngine &instance();

	void setConfig(SmartFilterConfig config);
	[[nodiscard]] SmartFilterConfig config() const;

	// Per-chat reaction filter controls (persisted in config).
	void setChatReactionFilter(int64_t chatId, bool enabled, int minReactions);
	[[nodiscard]] ReactionChatConfig chatReactionConfig(int64_t chatId) const;

	// Per-chat ad whitelist controls (persisted in config).
	void setChatAdWhitelisted(int64_t chatId, bool whitelisted);
	[[nodiscard]] bool isChatAdWhitelisted(int64_t chatId) const;

	// Runtime-only temporary reveal toggle (auto-reset on chat switch).
	void setTemporaryRevealAll(int64_t chatId, bool reveal);
	[[nodiscard]] bool isTemporaryRevealAll(int64_t chatId) const;
	void onActiveChatChanged(int64_t previousChatId, int64_t nextChatId);

	// Core filter evaluation.
	// When isSearchContext == true (Ctrl+F), reaction filter is bypassed while ad filter still runs.
	[[nodiscard]] FilterReason evaluate(
		const MessageSnapshot &msg,
		bool isSearchContext = false) const;

	[[nodiscard]] bool shouldHide(
		const MessageSnapshot &msg,
		bool isSearchContext = false) const {
		return evaluate(msg, isSearchContext) != FilterReason::None;
	}

	// Track per-chat hidden counts for the top filter bar.
	void recordEvaluation(int64_t chatId, int64_t messageId, FilterReason reason);
	void clearChatStats(int64_t chatId);
	[[nodiscard]] ChatHiddenStats chatStats(int64_t chatId) const;

	// Callback invoked when config or dynamic reactions change so UI can repaint.
	using ChangeCallback = std::function<void(int64_t chatId)>;
	void setOnChangeCallback(ChangeCallback cb);
	void notifyDynamicReactionChange(int64_t chatId, int64_t messageId);

private:
	struct CompiledRule {
		AdRule rule;
		std::optional<std::regex> compiledRegex;
		std::string normalizedPattern;
	};

	void recompileRulesLocked();
	[[nodiscard]] bool matchesAdRulesLocked(const MessageSnapshot &msg) const;
	[[nodiscard]] static bool matchSingleRule(
		const CompiledRule &compiled,
		const MessageSnapshot &msg);

	mutable std::mutex _mutex;
	SmartFilterConfig _config;
	std::vector<CompiledRule> _compiledRules;
	std::unordered_set<int64_t> _temporaryRevealChats;
	std::unordered_map<int64_t, std::unordered_map<int64_t, FilterReason>> _recordedReasons;
	ChangeCallback _onChange;
};

} // namespace Ayu::SmartFilter
