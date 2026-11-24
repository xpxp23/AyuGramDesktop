#pragma once

#include "smart_filter_engine.h"

class HistoryItem;

namespace Ayu::SmartFilter {

// Extracts all ad-matching fields and total reactions (including album aggregation)
// from a Telegram Desktop / AyuGram HistoryItem.
[[nodiscard]] MessageSnapshot buildSnapshotFromHistoryItem(
	not_null<const HistoryItem*> item);

// Convenience entry point intended to be called from FiltersController::filtered().
[[nodiscard]] bool shouldFilterHistoryItem(
	not_null<const HistoryItem*> item,
	bool isSearchContext = false);

// Call when a HistoryItem's reactions update in real time so hidden messages
// that cross >= threshold immediately become visible.
void handleHistoryItemReactionsUpdated(not_null<const HistoryItem*> item);

} // namespace Ayu::SmartFilter
