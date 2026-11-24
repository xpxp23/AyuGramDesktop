#include "smart_filter_backfill.h"

#include <algorithm>

namespace Ayu::SmartFilter {

SmartFilterBackfillController::SmartFilterBackfillController(
	int minVisibleItems,
	int maxConsecutivePages)
: _minVisibleItems(std::max(1, minVisibleItems))
, _maxConsecutivePages(std::max(1, maxConsecutivePages)) {
}

void SmartFilterBackfillController::setLimits(
		int minVisibleItems,
		int maxConsecutivePages) {
	_minVisibleItems = std::max(1, minVisibleItems);
	_maxConsecutivePages = std::max(1, maxConsecutivePages);
}

void SmartFilterBackfillController::setCallbacks(
		LoadOlderCallback loadOlder,
		CapChangedCallback onCapChanged) {
	_loadOlder = std::move(loadOlder);
	_onCapChanged = std::move(onCapChanged);
}

void SmartFilterBackfillController::onChatSwitched(int64_t chatId) {
	_state.activeChatId = chatId;
	_state.consecutiveFetches = 0;
	_state.requestInFlight = false;
	setCapReached(false);
}

void SmartFilterBackfillController::onUserManualScroll() {
	_state.consecutiveFetches = 0;
	setCapReached(false);
}

void SmartFilterBackfillController::onManualContinueClicked() {
	_state.consecutiveFetches = 0;
	setCapReached(false);
	if (!_state.requestInFlight && _loadOlder) {
		_state.requestInFlight = true;
		_state.consecutiveFetches = 1;
		_loadOlder();
	}
}

void SmartFilterBackfillController::onViewportEvaluated(
		int visibleItemCount,
		bool loadedAtTop) {
	maybeTriggerFetch(visibleItemCount, loadedAtTop);
}

void SmartFilterBackfillController::onFetchCompleted(
		int visibleItemCount,
		bool loadedAtTop) {
	_state.requestInFlight = false;
	maybeTriggerFetch(visibleItemCount, loadedAtTop);
}

void SmartFilterBackfillController::setCapReached(bool reached) {
	if (_state.capReached == reached) {
		return;
	}
	_state.capReached = reached;
	if (_onCapChanged) {
		_onCapChanged(reached);
	}
}

void SmartFilterBackfillController::maybeTriggerFetch(
		int visibleItemCount,
		bool loadedAtTop) {
	if (visibleItemCount >= _minVisibleItems || loadedAtTop) {
		setCapReached(false);
		return;
	}
	if (_state.requestInFlight) {
		return;
	}
	if (_state.consecutiveFetches >= _maxConsecutivePages) {
		setCapReached(true);
		return;
	}
	if (!_loadOlder) {
		return;
	}
	_state.requestInFlight = true;
	++_state.consecutiveFetches;
	_loadOlder();
}

} // namespace Ayu::SmartFilter
