#include "smart_filter_bar.h"

#include "ayu/features/filters/smart_filter_config.h"
#include "ayu/features/filters/smart_filter_engine.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_ayu_smart_filter.h"

namespace Ayu::SmartFilter {
namespace {

constexpr auto kThresholdStep = 5;

} // namespace

SmartFilterBar::SmartFilterBar(QWidget *parent)
: Ui::RpWidget(parent)
, _toggleButton(this, rpl::single(u"筛选: 关"_q), st::smartFilterBarButton)
, _minusButton(this, rpl::single(u"-"_q), st::smartFilterBarButton)
, _thresholdLabel(this, QString(), st::smartFilterBarLabel)
, _plusButton(this, rpl::single(u"+"_q), st::smartFilterBarButton)
, _statsLabel(this, QString(), st::smartFilterBarLabel)
, _revealButton(this, rpl::single(u"临时显示全部"_q), st::smartFilterBarButton)
, _loadOlderButton(this, rpl::single(u"继续加载更早消息"_q), st::smartFilterBarButton) {
	_toggleButton->setClickedCallback([=] { toggleReactionEnabled(); });
	_minusButton->setClickedCallback([=] { adjustThreshold(-kThresholdStep); });
	_plusButton->setClickedCallback([=] { adjustThreshold(kThresholdStep); });
	_revealButton->setClickedCallback([=] { toggleRevealAll(); });
	_loadOlderButton->setClickedCallback([=] {
		if (_onManualLoadOlder) {
			_onManualLoadOlder();
		}
	});
	_loadOlderButton->hide();
	resize(width(), st::smartFilterBarHeight);
}

void SmartFilterBar::setActiveChat(int64_t chatId) {
	if (_chatId == chatId) {
		return;
	}
	const auto prev = _chatId;
	_chatId = chatId;
	_backfillCapReached = false;
	_loadOlderButton->hide();
	SmartFilterEngine::instance().onActiveChatChanged(prev, chatId);
	updateLabels();
	resizeToWidth(width());
}

void SmartFilterBar::refreshStats() {
	updateLabels();
	resizeToWidth(width());
}

void SmartFilterBar::setBackfillCapReached(bool reached) {
	_backfillCapReached = reached;
	_loadOlderButton->setVisible(reached);
	resizeToWidth(width());
}

void SmartFilterBar::setOnFilterToggled(std::function<void()> callback) {
	_onFilterToggled = std::move(callback);
}

void SmartFilterBar::setOnManualLoadOlder(std::function<void()> callback) {
	_onManualLoadOlder = std::move(callback);
}

void SmartFilterBar::toggleReactionEnabled() {
	if (_chatId == 0) return;
	auto &engine = SmartFilterEngine::instance();
	const auto cfg = engine.chatReactionConfig(_chatId);
	engine.setChatReactionFilter(_chatId, !cfg.enabled, cfg.minReactions);
	updateLabels();
	if (_onFilterToggled) {
		_onFilterToggled();
	}
}

void SmartFilterBar::adjustThreshold(int delta) {
	if (_chatId == 0) return;
	auto &engine = SmartFilterEngine::instance();
	const auto cfg = engine.chatReactionConfig(_chatId);
	const int nextThreshold = std::max(0, cfg.minReactions + delta);
	engine.setChatReactionFilter(_chatId, true, nextThreshold);
	updateLabels();
	if (_onFilterToggled) {
		_onFilterToggled();
	}
}

void SmartFilterBar::toggleRevealAll() {
	if (_chatId == 0) return;
	auto &engine = SmartFilterEngine::instance();
	const bool nextReveal = !engine.isTemporaryRevealAll(_chatId);
	engine.setTemporaryRevealAll(_chatId, nextReveal);
	updateLabels();
	if (_onFilterToggled) {
		_onFilterToggled();
	}
}

void SmartFilterBar::updateLabels() {
	auto &engine = SmartFilterEngine::instance();
	const auto cfg = engine.chatReactionConfig(_chatId);
	const auto stats = engine.chatStats(_chatId);
	const bool revealing = engine.isTemporaryRevealAll(_chatId);

	_toggleButton->setText(rpl::single(
		cfg.enabled ? u"表情筛选: 开"_q : u"表情筛选: 关"_q));
	_thresholdLabel->setText(u"≥ %1"_q.arg(cfg.minReactions));

	const int lowAndService = stats.lowReactionHidden + stats.serviceHidden;
	if (revealing) {
		_statsLabel->setText(u"当前已临时显示全部（共 %1 条匹配隐藏）"_q.arg(stats.total()));
		_revealButton->setText(rpl::single(u"恢复隐藏"_q));
	} else {
		_statsLabel->setText(u"已隐藏 %1 条（广告 %2 / 低赞 %3）"_q
			.arg(stats.total())
			.arg(stats.adHidden)
			.arg(lowAndService));
		_revealButton->setText(rpl::single(u"临时显示全部"_q));
	}
}

int SmartFilterBar::resizeGetHeight(int newWidth) {
	const auto pad = st::smartFilterBarPadding;
	const auto spacing = st::smartFilterBarItemSpacing;
	const int height = st::smartFilterBarHeight;
	int left = pad.left();
	int right = newWidth - pad.right();

	auto placeLeft = [&](Ui::RpWidget *w) {
		if (!w || w->isHidden()) return;
		const int top = (height - w->height()) / 2;
		w->moveToLeft(left, top, newWidth);
		left += w->width() + spacing;
	};
	auto placeRight = [&](Ui::RpWidget *w) {
		if (!w || w->isHidden()) return;
		right -= w->width();
		const int top = (height - w->height()) / 2;
		w->moveToLeft(right, top, newWidth);
		right -= spacing;
	};

	placeLeft(_toggleButton.data());
	placeLeft(_minusButton.data());
	_thresholdLabel->resizeToNaturalWidth(st::smartFilterBarThresholdWidth);
	placeLeft(_thresholdLabel.data());
	placeLeft(_plusButton.data());

	placeRight(_revealButton.data());
	if (_backfillCapReached) {
		placeRight(_loadOlderButton.data());
	}

	const int statsWidth = std::max(0, right - left);
	_statsLabel->resizeToWidth(statsWidth);
	_statsLabel->moveToLeft(left, (height - _statsLabel->height()) / 2, newWidth);
	return height;
}

void SmartFilterBar::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(e->rect(), st::windowBg);
	p.fillRect(0, height() - st::lineWidth, width(), st::lineWidth, st::shadowFg);
}

} // namespace Ayu::SmartFilter
