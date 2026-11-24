#pragma once

#include "ui/rp_widget.h"

#include <cstdint>
#include <functional>

namespace Ui {
class FlatLabel;
class RoundButton;
} // namespace Ui

namespace Ayu::SmartFilter {

// Compact horizontal bar mounted directly beneath HistoryWidget's TopBar.
// Shows per-chat reaction threshold controls, live hidden counts,
// a one-click temporary reveal toggle, and a conditional backfill button.
class SmartFilterBar final : public Ui::RpWidget {
public:
	explicit SmartFilterBar(QWidget *parent);

	void setActiveChat(int64_t chatId);
	void refreshStats();
	void setBackfillCapReached(bool reached);

	void setOnFilterToggled(std::function<void()> callback);
	void setOnManualLoadOlder(std::function<void()> callback);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void updateLabels();
	void adjustThreshold(int delta);
	void toggleRevealAll();
	void toggleReactionEnabled();

	int64_t _chatId = 0;
	bool _backfillCapReached = false;

	object_ptr<Ui::RoundButton> _toggleButton;
	object_ptr<Ui::RoundButton> _minusButton;
	object_ptr<Ui::FlatLabel> _thresholdLabel;
	object_ptr<Ui::RoundButton> _plusButton;
	object_ptr<Ui::FlatLabel> _statsLabel;
	object_ptr<Ui::RoundButton> _revealButton;
	object_ptr<Ui::RoundButton> _loadOlderButton;

	std::function<void()> _onFilterToggled;
	std::function<void()> _onManualLoadOlder;
};

} // namespace Ayu::SmartFilter
