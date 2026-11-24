#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ayu::SmartFilter {

enum class AdRuleType {
	Keyword,
	Regex,
	Domain,
	Channel,
};

struct AdRule {
	std::string id;
	bool enabled = true;
	AdRuleType type = AdRuleType::Keyword;
	std::string pattern;
	bool caseSensitive = false;
	std::optional<int64_t> chatId; // std::nullopt = global rule
	std::string note;
};

struct ReactionChatConfig {
	bool enabled = false;
	int minReactions = 30;
	bool exemptPinned = true;
	bool hideServiceMessages = true;
};

struct SmartFilterConfig {
	int schemaVersion = 1;
	bool adFilterEnabled = true;
	bool hideSponsoredMessages = true;
	int maxBackfillPages = 5;
	int minVisibleItems = 5;
	std::unordered_set<int64_t> adWhitelistChatIds;
	std::vector<AdRule> adRules;
	std::unordered_map<int64_t, ReactionChatConfig> chatReactionConfigs;
};

[[nodiscard]] std::string adRuleTypeToString(AdRuleType type);
[[nodiscard]] AdRuleType adRuleTypeFromString(const std::string &value);

[[nodiscard]] std::string serializeConfigToJson(const SmartFilterConfig &config);
[[nodiscard]] std::optional<SmartFilterConfig> deserializeConfigFromJson(
	const std::string &jsonText,
	std::string *errorOut = nullptr);

[[nodiscard]] bool saveConfigToFile(
	const std::string &filePath,
	const SmartFilterConfig &config,
	std::string *errorOut = nullptr);

[[nodiscard]] std::optional<SmartFilterConfig> loadConfigFromFile(
	const std::string &filePath,
	std::string *errorOut = nullptr);

} // namespace Ayu::SmartFilter
