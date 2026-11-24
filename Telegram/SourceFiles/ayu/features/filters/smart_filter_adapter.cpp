#include "smart_filter_adapter.h"

#include "data/data_groups.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"

namespace Ayu::SmartFilter {
namespace {

int sumSingleItemReactions(not_null<const HistoryItem*> item) {
	int total = 0;
	for (const auto &entry : item->reactions()) {
		total += std::max(0, entry.count);
	}
	return total;
}

int computeAlbumAwareReactions(not_null<const HistoryItem*> item) {
	const auto groupId = item->groupId();
	if (!groupId) {
		return sumSingleItemReactions(item);
	}
	const auto group = item->history()->owner().groups().find(item);
	if (!group || group->items.empty()) {
		return sumSingleItemReactions(item);
	}
	// WHY: In Telegram albums, reactions are typically attached to the leader
	// item, but summing across all items in the group guarantees every media
	// tile in the album receives the exact same reaction total.
	int albumTotal = 0;
	for (const auto &member : group->items) {
		albumTotal += sumSingleItemReactions(member);
	}
	return albumTotal;
}

void collectAdFields(
		not_null<const HistoryItem*> item,
		MessageSnapshot &out) {
	const auto original = item->originalText();
	if (!original.text.isEmpty()) {
		if (!out.textAndCaption.empty()) {
			out.textAndCaption.push_back('\n');
		}
		out.textAndCaption += original.text.toStdString();
	}
	for (const auto &entity : original.entities) {
		const auto type = entity.type();
		if (type == EntityType::CustomUrl || type == EntityType::Url) {
			const auto data = entity.data();
			if (!data.isEmpty()) {
				out.entityUrls.push_back(data.toStdString());
			} else if (entity.offset() >= 0 && entity.length() > 0) {
				out.entityUrls.push_back(
					original.text.mid(entity.offset(), entity.length()).toStdString());
			}
		}
	}

	if (const auto markup = item->inlineReplyMarkup(); markup && !markup->data.isNull()) {
		for (const auto &row : markup->data.rows) {
			for (const auto &button : row) {
				if (!button.text.isEmpty()) {
					out.inlineButtonTexts.push_back(button.text.toStdString());
				}
				if (!button.data.isEmpty()) {
					out.inlineButtonUrls.push_back(
						QString::fromUtf8(button.data).toStdString());
				}
			}
		}
	}

	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->savedFromPeer) {
			out.forwardedFromName = forwarded->savedFromPeer->name().toStdString();
			out.forwardedFromUsername = forwarded->savedFromPeer->username().toStdString();
		} else if (forwarded->originalSender) {
			out.forwardedFromName = forwarded->originalSender->name().toStdString();
			out.forwardedFromUsername = forwarded->originalSender->username().toStdString();
		} else if (forwarded->originalHiddenSenderInfo) {
			out.forwardedFromName = forwarded->originalHiddenSenderInfo->name.toStdString();
		}
	}
	if (out.forwardedFromUsername.empty()) {
		if (const auto originalFrom = item->fromOriginal(); originalFrom != item->from()) {
			if (out.forwardedFromName.empty()) {
				out.forwardedFromName = originalFrom->name().toStdString();
			}
			out.forwardedFromUsername = originalFrom->username().toStdString();
		}
	}
}

} // namespace

MessageSnapshot buildSnapshotFromHistoryItem(not_null<const HistoryItem*> item) {
	MessageSnapshot snap;
	snap.chatId = static_cast<int64_t>(item->history()->peer->id.value);
	snap.messageId = static_cast<int64_t>(item->id.bare);
	snap.mediaGroupId = item->groupId().value;
	snap.isSponsored = item->isSponsored();
	snap.isPinned = item->isPinned();
	snap.isService = item->isService();
	snap.totalReactions = computeAlbumAwareReactions(item);

	if (const auto groupId = item->groupId()) {
		if (const auto group = item->history()->owner().groups().find(item)) {
			for (const auto &member : group->items) {
				collectAdFields(member, snap);
			}
			return snap;
		}
	}
	collectAdFields(item, snap);
	return snap;
}

bool shouldFilterHistoryItem(
		not_null<const HistoryItem*> item,
		bool isSearchContext) {
	const auto snap = buildSnapshotFromHistoryItem(item);
	const auto reason = SmartFilterEngine::instance().evaluate(snap, isSearchContext);
	if (!isSearchContext) {
		SmartFilterEngine::instance().recordEvaluation(snap.chatId, snap.messageId, reason);
	}
	return reason != FilterReason::None;
}

void handleHistoryItemReactionsUpdated(not_null<const HistoryItem*> item) {
	const auto chatId = static_cast<int64_t>(item->history()->peer->id.value);
	const auto msgId = static_cast<int64_t>(item->id.bare);
	auto &engine = SmartFilterEngine::instance();
	if (!engine.chatReactionConfig(chatId).enabled) {
		return;
	}
	const auto snap = buildSnapshotFromHistoryItem(item);
	const auto reason = engine.evaluate(snap, false);
	engine.recordEvaluation(chatId, msgId, reason);
	engine.notifyDynamicReactionChange(chatId, msgId);
}

} // namespace Ayu::SmartFilter
