#pragma once

#include <cstdint>
#include <functional>

namespace Ayu::SmartFilter {

struct BackfillState {
	int64_t activeChatId = 0;
	int consecutiveFetches = 0;
	bool requestInFlight = false;
	bool capReached = false;
};

// Pure state machine guarding history backfill against blank screens & FloodWait.
class SmartFilterBackfillController {
public:
	using LoadOlderCallback = std::function<void()>;
	using CapChangedCallback = std::function<void(bool capReached)>;

	SmartFilterBackfillController(
		int minVisibleItems = 5,
		int maxConsecutivePages = 5);

	void setLimits(int minVisibleItems, int maxConsecutivePages);
	void setCallbacks(LoadOlderCallback loadOlder, CapChangedCallback onCapChanged);

	void onChatSwitched(int64_t chatId);
	void onUserManualScroll();
	void onManualContinueClicked();

	// Call whenever the history view finishes layout or receives a new history page.
	void onViewportEvaluated(int visibleItemCount, bool loadedAtTop);
	void onFetchCompleted(int visibleItemCount, bool loadedAtTop);

	[[nodiscard]] const BackfillState &state() const {
		return _state;
	}

private:
	void setCapReached(bool reached);
	void maybeTriggerFetch(int visibleItemCount, bool loadedAtTop);

	int _minVisibleItems = 5;
	int _maxConsecutivePages = 5;
	BackfillState _state;
	LoadOlderCallback _loadOlder;
	CapChangedCallback _onCapChanged;
};

} // namespace Ayu::SmartFilter
