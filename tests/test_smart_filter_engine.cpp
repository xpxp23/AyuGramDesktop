#include "ayu/features/filters/smart_filter_backfill.h"
#include "ayu/features/filters/smart_filter_config.h"
#include "ayu/features/filters/smart_filter_engine.h"

#include <cassert>
#include <iostream>

using namespace Ayu::SmartFilter;

namespace {

void testReactionThresholdAndCounting() {
	auto &engine = SmartFilterEngine::instance();
	SmartFilterConfig cfg;
	cfg.adFilterEnabled = true;
	cfg.chatReactionConfigs[1001] = ReactionChatConfig{
		.enabled = true,
		.minReactions = 30,
		.exemptPinned = true,
		.hideServiceMessages = true,
	};
	engine.setConfig(cfg);

	// 10 + 15 = 25 < 30 -> hidden
	MessageSnapshot lowMsg;
	lowMsg.chatId = 1001;
	lowMsg.messageId = 1;
	lowMsg.totalReactions = 10 + 15;
	assert(engine.evaluate(lowMsg) == FilterReason::LowReactions);

	// Exactly 30 >= 30 -> visible
	MessageSnapshot exactMsg = lowMsg;
	exactMsg.messageId = 2;
	exactMsg.totalReactions = 30;
	assert(engine.evaluate(exactMsg) == FilterReason::None);

	// Dynamic update: message grows from 25 -> 32 -> auto-unhidden
	lowMsg.totalReactions = 32;
	assert(engine.evaluate(lowMsg) == FilterReason::None);

	// Pinned message with 0 reactions -> exempt
	MessageSnapshot pinnedMsg;
	pinnedMsg.chatId = 1001;
	pinnedMsg.messageId = 3;
	pinnedMsg.isPinned = true;
	pinnedMsg.totalReactions = 0;
	assert(engine.evaluate(pinnedMsg) == FilterReason::None);

	// Service message when filter active -> hidden
	MessageSnapshot serviceMsg;
	serviceMsg.chatId = 1001;
	serviceMsg.messageId = 4;
	serviceMsg.isService = true;
	assert(engine.evaluate(serviceMsg) == FilterReason::ServiceMessage);

	// Other chat (filter off by default) -> visible even with 0 reactions
	MessageSnapshot otherChatMsg;
	otherChatMsg.chatId = 9999;
	otherChatMsg.messageId = 5;
	otherChatMsg.totalReactions = 0;
	assert(engine.evaluate(otherChatMsg) == FilterReason::None);
}

void testAdPriorityAndMultiFieldMatching() {
	auto &engine = SmartFilterEngine::instance();
	SmartFilterConfig cfg;
	cfg.adFilterEnabled = true;
	cfg.hideSponsoredMessages = true;
	cfg.adRules = {
		AdRule{ "r1", true, AdRuleType::Keyword, "商务合作", false, std::nullopt, "" },
		AdRule{ "r2", true, AdRuleType::Regex, "(优惠码|邀请码)", false, std::nullopt, "" },
		AdRule{ "r3", true, AdRuleType::Domain, "spam-shop.example", false, std::nullopt, "" },
		AdRule{ "r4", true, AdRuleType::Channel, "@ad_channel_bot", false, std::nullopt, "" },
		AdRule{ "r5", true, AdRuleType::Keyword, "专属暗号", false, 2002, "per-chat rule" },
	};
	cfg.chatReactionConfigs[2001] = ReactionChatConfig{ true, 30, true, true };
	engine.setConfig(cfg);

	// Sponsored ad with 100 reactions -> still hidden (Ad > Reactions)
	MessageSnapshot sponsored;
	sponsored.chatId = 2001;
	sponsored.messageId = 10;
	sponsored.isSponsored = true;
	sponsored.totalReactions = 100;
	assert(engine.evaluate(sponsored) == FilterReason::SponsoredAd);

	// Inline button URL matches domain rule despite 80 reactions -> hidden
	MessageSnapshot btnAd;
	btnAd.chatId = 2001;
	btnAd.messageId = 11;
	btnAd.totalReactions = 80;
	btnAd.inlineButtonTexts = { "立即领取" };
	btnAd.inlineButtonUrls = { "https://go.spam-shop.example/deal?ref=1" };
	assert(engine.evaluate(btnAd) == FilterReason::CustomAdRule);

	// Forwarded channel username matches channel rule -> hidden
	MessageSnapshot fwdAd;
	fwdAd.chatId = 2001;
	fwdAd.messageId = 12;
	fwdAd.totalReactions = 60;
	fwdAd.forwardedFromUsername = "ad_channel_bot";
	assert(engine.evaluate(fwdAd) == FilterReason::CustomAdRule);

	// Per-chat ad rule only triggers in chat 2002, not 2001
	MessageSnapshot scopedMsg;
	scopedMsg.chatId = 2001;
	scopedMsg.messageId = 13;
	scopedMsg.totalReactions = 50;
	scopedMsg.textAndCaption = "今天分享专属暗号";
	assert(engine.evaluate(scopedMsg) == FilterReason::None);
	scopedMsg.chatId = 2002;
	assert(engine.evaluate(scopedMsg) == FilterReason::CustomAdRule);

	// Whitelist chat skips ad filter
	engine.setChatAdWhitelisted(2001, true);
	assert(engine.evaluate(btnAd) == FilterReason::None);
	engine.setChatAdWhitelisted(2001, false);
}

void testSearchBypassAndTemporaryReveal() {
	auto &engine = SmartFilterEngine::instance();

	// Low reaction message in Ctrl+F search -> visible (bypasses reaction filter)
	MessageSnapshot lowMsg;
	lowMsg.chatId = 2001;
	lowMsg.messageId = 20;
	lowMsg.totalReactions = 5;
	assert(engine.evaluate(lowMsg, false) == FilterReason::LowReactions);
	assert(engine.evaluate(lowMsg, true) == FilterReason::None);

	// Ad in Ctrl+F search -> STILL hidden
	MessageSnapshot adMsg;
	adMsg.chatId = 2001;
	adMsg.messageId = 21;
	adMsg.totalReactions = 50;
	adMsg.textAndCaption = "欢迎商务合作";
	assert(engine.evaluate(adMsg, true) == FilterReason::CustomAdRule);

	// Temporary reveal all shows everything, and resets when switching chat
	engine.setTemporaryRevealAll(2001, true);
	assert(engine.evaluate(lowMsg, false) == FilterReason::None);
	assert(engine.evaluate(adMsg, false) == FilterReason::None);

	engine.onActiveChatChanged(2001, 3001);
	assert(!engine.isTemporaryRevealAll(2001));
	assert(engine.evaluate(lowMsg, false) == FilterReason::LowReactions);
}

void testBackfillCapAndReset() {
	int fetchCalls = 0;
	bool capHit = false;
	SmartFilterBackfillController bf(5, 5);
	bf.setCallbacks(
		[&] { ++fetchCalls; },
		[&](bool reached) { capHit = reached; });

	bf.onChatSwitched(1001);
	// Simulate 5 pages returning only 2 visible items each
	bf.onViewportEvaluated(2, false);
	assert(fetchCalls == 1 && !capHit);
	for (int i = 2; i <= 5; ++i) {
		bf.onFetchCompleted(2, false);
		assert(fetchCalls == i && !capHit);
	}
	// 5th fetch completes still with < 5 visible -> cap reached, no 6th auto fetch
	bf.onFetchCompleted(2, false);
	assert(fetchCalls == 5 && capHit);

	// Manual continue resets cap and triggers next fetch
	bf.onManualContinueClicked();
	assert(fetchCalls == 6 && !capHit);
}

void testStatsAndJsonSerialization() {
	auto &engine = SmartFilterEngine::instance();
	engine.clearChatStats(1001);
	engine.recordEvaluation(1001, 1, FilterReason::SponsoredAd);
	engine.recordEvaluation(1001, 2, FilterReason::CustomAdRule);
	engine.recordEvaluation(1001, 3, FilterReason::LowReactions);
	engine.recordEvaluation(1001, 4, FilterReason::ServiceMessage);
	const auto stats = engine.chatStats(1001);
	assert(stats.adHidden == 2);
	assert(stats.lowReactionHidden == 1);
	assert(stats.serviceHidden == 1);
	assert(stats.total() == 4);

	// Re-evaluate message 3 as visible -> decrements stats
	engine.recordEvaluation(1001, 3, FilterReason::None);
	assert(engine.chatStats(1001).lowReactionHidden == 0);

	const auto jsonText = serializeConfigToJson(engine.config());
	assert(jsonText.find("\"ad_filter_enabled\": true") != std::string::npos);
	assert(jsonText.find("\"min_reactions\": 30") != std::string::npos);
}

} // namespace

int main() {
	testReactionThresholdAndCounting();
	testAdPriorityAndMultiFieldMatching();
	testSearchBypassAndTemporaryReveal();
	testBackfillCapAndReset();
	testStatsAndJsonSerialization();
	std::cout << "ALL SMART FILTER UNIT TESTS PASSED!\n";
	return 0;
}
