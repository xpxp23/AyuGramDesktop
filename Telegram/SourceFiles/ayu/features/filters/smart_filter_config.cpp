#include "smart_filter_config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

// In AyuGramDesktop, nlohmann/json is already bundled.
// A lightweight fallback parser/serializer is included below when building
// standalone unit tests without nlohmann/json in include path.
#if __has_include("ayu/libs/json.hpp")
#include "ayu/libs/json.hpp"
#define AYU_SF_HAS_NLOHMANN_JSON 1
#elif __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#define AYU_SF_HAS_NLOHMANN_JSON 1
#else
#define AYU_SF_HAS_NLOHMANN_JSON 0
#endif

namespace Ayu::SmartFilter {

std::string adRuleTypeToString(AdRuleType type) {
	switch (type) {
	case AdRuleType::Keyword: return "keyword";
	case AdRuleType::Regex: return "regex";
	case AdRuleType::Domain: return "domain";
	case AdRuleType::Channel: return "channel";
	}
	return "keyword";
}

AdRuleType adRuleTypeFromString(const std::string &value) {
	std::string lower = value;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	if (lower == "regex") return AdRuleType::Regex;
	if (lower == "domain") return AdRuleType::Domain;
	if (lower == "channel") return AdRuleType::Channel;
	return AdRuleType::Keyword;
}

#if AYU_SF_HAS_NLOHMANN_JSON

std::string serializeConfigToJson(const SmartFilterConfig &config) {
	using json = nlohmann::json;
	json root;
	root["schema_version"] = config.schemaVersion;
	root["ad_filter_enabled"] = config.adFilterEnabled;
	root["hide_sponsored_messages"] = config.hideSponsoredMessages;
	root["max_backfill_pages"] = config.maxBackfillPages;
	root["min_visible_items"] = config.minVisibleItems;

	json whitelist = json::array();
	for (const auto id : config.adWhitelistChatIds) {
		whitelist.push_back(id);
	}
	root["ad_whitelist_chat_ids"] = std::move(whitelist);

	json rules = json::array();
	for (const auto &rule : config.adRules) {
		json item;
		item["id"] = rule.id;
		item["enabled"] = rule.enabled;
		item["type"] = adRuleTypeToString(rule.type);
		item["pattern"] = rule.pattern;
		item["case_sensitive"] = rule.caseSensitive;
		if (rule.chatId.has_value()) {
			item["chat_id"] = *rule.chatId;
		} else {
			item["chat_id"] = nullptr;
		}
		item["note"] = rule.note;
		rules.push_back(std::move(item));
	}
	root["ad_rules"] = std::move(rules);

	json chats = json::object();
	for (const auto &[chatId, cfg] : config.chatReactionConfigs) {
		json c;
		c["enabled"] = cfg.enabled;
		c["min_reactions"] = cfg.minReactions;
		c["exempt_pinned"] = cfg.exemptPinned;
		c["hide_service_messages"] = cfg.hideServiceMessages;
		chats[std::to_string(chatId)] = std::move(c);
	}
	root["chat_reaction_configs"] = std::move(chats);

	return root.dump(2);
}

std::optional<SmartFilterConfig> deserializeConfigFromJson(
		const std::string &jsonText,
		std::string *errorOut) {
	try {
		const auto root = nlohmann::json::parse(jsonText);
		SmartFilterConfig config;
		config.schemaVersion = root.value("schema_version", 1);
		config.adFilterEnabled = root.value("ad_filter_enabled", true);
		config.hideSponsoredMessages = root.value("hide_sponsored_messages", true);
		config.maxBackfillPages = std::max(1, root.value("max_backfill_pages", 5));
		config.minVisibleItems = std::max(1, root.value("min_visible_items", 5));

		if (root.contains("ad_whitelist_chat_ids") && root["ad_whitelist_chat_ids"].is_array()) {
			for (const auto &id : root["ad_whitelist_chat_ids"]) {
				if (id.is_number_integer()) {
					config.adWhitelistChatIds.insert(id.get<int64_t>());
				}
			}
		}

		if (root.contains("ad_rules") && root["ad_rules"].is_array()) {
			for (const auto &item : root["ad_rules"]) {
				if (!item.is_object()) continue;
				AdRule rule;
				rule.id = item.value("id", "");
				rule.enabled = item.value("enabled", true);
				rule.type = adRuleTypeFromString(item.value("type", "keyword"));
				rule.pattern = item.value("pattern", "");
				rule.caseSensitive = item.value("case_sensitive", false);
				if (item.contains("chat_id") && item["chat_id"].is_number_integer()) {
					rule.chatId = item["chat_id"].get<int64_t>();
				}
				rule.note = item.value("note", "");
				if (!rule.pattern.empty()) {
					config.adRules.push_back(std::move(rule));
				}
			}
		}

		if (root.contains("chat_reaction_configs") && root["chat_reaction_configs"].is_object()) {
			for (const auto &[key, val] : root["chat_reaction_configs"].items()) {
				if (!val.is_object()) continue;
				const auto chatId = std::stoll(key);
				ReactionChatConfig cfg;
				cfg.enabled = val.value("enabled", false);
				cfg.minReactions = std::max(0, val.value("min_reactions", 30));
				cfg.exemptPinned = val.value("exempt_pinned", true);
				cfg.hideServiceMessages = val.value("hide_service_messages", true);
				config.chatReactionConfigs[chatId] = cfg;
			}
		}
		return config;
	} catch (const std::exception &ex) {
		if (errorOut) {
			*errorOut = ex.what();
		}
		return std::nullopt;
	}
}

#else

namespace {

std::string escapeJson(const std::string &s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: out.push_back(c); break;
		}
	}
	return out;
}

} // namespace

std::string serializeConfigToJson(const SmartFilterConfig &config) {
	std::ostringstream oss;
	oss << "{\n";
	oss << "  \"schema_version\": " << config.schemaVersion << ",\n";
	oss << "  \"ad_filter_enabled\": " << (config.adFilterEnabled ? "true" : "false") << ",\n";
	oss << "  \"hide_sponsored_messages\": " << (config.hideSponsoredMessages ? "true" : "false") << ",\n";
	oss << "  \"max_backfill_pages\": " << config.maxBackfillPages << ",\n";
	oss << "  \"min_visible_items\": " << config.minVisibleItems << ",\n";
	oss << "  \"ad_whitelist_chat_ids\": [";
	bool first = true;
	for (auto id : config.adWhitelistChatIds) {
		if (!first) oss << ", ";
		first = false;
		oss << id;
	}
	oss << "],\n";
	oss << "  \"ad_rules\": [\n";
	for (size_t i = 0; i < config.adRules.size(); ++i) {
		const auto &r = config.adRules[i];
		oss << "    {\n";
		oss << "      \"id\": \"" << escapeJson(r.id) << "\",\n";
		oss << "      \"enabled\": " << (r.enabled ? "true" : "false") << ",\n";
		oss << "      \"type\": \"" << adRuleTypeToString(r.type) << "\",\n";
		oss << "      \"pattern\": \"" << escapeJson(r.pattern) << "\",\n";
		oss << "      \"case_sensitive\": " << (r.caseSensitive ? "true" : "false") << ",\n";
		oss << "      \"chat_id\": ";
		if (r.chatId.has_value()) oss << *r.chatId;
		else oss << "null";
		oss << ",\n";
		oss << "      \"note\": \"" << escapeJson(r.note) << "\"\n";
		oss << "    }" << (i + 1 < config.adRules.size() ? "," : "") << "\n";
	}
	oss << "  ],\n";
	oss << "  \"chat_reaction_configs\": {\n";
	size_t idx = 0;
	for (const auto &[cid, cfg] : config.chatReactionConfigs) {
		oss << "    \"" << cid << "\": {\n";
		oss << "      \"enabled\": " << (cfg.enabled ? "true" : "false") << ",\n";
		oss << "      \"min_reactions\": " << cfg.minReactions << ",\n";
		oss << "      \"exempt_pinned\": " << (cfg.exemptPinned ? "true" : "false") << ",\n";
		oss << "      \"hide_service_messages\": " << (cfg.hideServiceMessages ? "true" : "false") << "\n";
		oss << "    }" << (++idx < config.chatReactionConfigs.size() ? "," : "") << "\n";
	}
	oss << "  }\n";
	oss << "}\n";
	return oss.str();
}

std::optional<SmartFilterConfig> deserializeConfigFromJson(
		const std::string &,
		std::string *errorOut) {
	if (errorOut) {
		*errorOut = "nlohmann/json header is required for JSON deserialization";
	}
	return std::nullopt;
}

#endif

bool saveConfigToFile(
		const std::string &filePath,
		const SmartFilterConfig &config,
		std::string *errorOut) {
	const auto tmpPath = filePath + ".tmp";
	std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		if (errorOut) *errorOut = "Failed to open temp config file for writing: " + tmpPath;
		return false;
	}
	const auto payload = serializeConfigToJson(config);
	out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
	out.close();
	if (!out) {
		if (errorOut) *errorOut = "Failed to flush temp config file: " + tmpPath;
		return false;
	}
	std::remove(filePath.c_str());
	if (std::rename(tmpPath.c_str(), filePath.c_str()) != 0) {
		if (errorOut) *errorOut = "Failed to rename temp config file to: " + filePath;
		return false;
	}
	return true;
}

std::optional<SmartFilterConfig> loadConfigFromFile(
		const std::string &filePath,
		std::string *errorOut) {
	std::ifstream in(filePath, std::ios::binary);
	if (!in.is_open()) {
		if (errorOut) *errorOut = "Config file does not exist: " + filePath;
		return std::nullopt;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return deserializeConfigFromJson(ss.str(), errorOut);
}

} // namespace Ayu::SmartFilter
