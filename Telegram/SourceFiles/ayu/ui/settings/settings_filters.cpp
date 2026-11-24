// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_filters.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/features/filters/filters_cache_controller.h"
#include "ayu/features/filters/smart_filter_config.h"
#include "ayu/features/filters/smart_filter_engine.h"
#include "ayu/ui/boxes/import_filters_box.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_main.h"
#include "ayu/utils/telegram_helpers.h"
#include "boxes/abstract_box.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "filters/per_dialog_filter.h"
#include "filters/settings_filters_list.h"
#include "inline_bots/bot_attach_web_view.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

#include <QtWidgets/QFileDialog>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <regex>

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

void BuildFiltersSettings(SectionBuilder &builder) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_RegexFilters());

	const auto enabledButton = builder.addButton({
		.id = u"ayu/filtersEnabled"_q,
		.title = tr::ayu_RegexFiltersEnable(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabled()),
	});
	if (enabledButton) {
		enabledButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabled());
		}) | on_next([=](bool enabled) {
			AyuSettings::getInstance().setFiltersEnabled(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, enabledButton->lifetime());
	}

	const auto sharedButton = builder.addButton({
		.id = u"ayu/filtersEnabledInChats"_q,
		.altIds = { u"ayu/filtersInChats"_q },
		.title = tr::ayu_RegexFiltersEnableSharedInChats(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabledInChats()),
	});
	if (sharedButton) {
		sharedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabledInChats());
		}) | on_next([=](bool enabled) {
			AyuSettings::getInstance().setFiltersEnabledInChats(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, sharedButton->lifetime());
	}

	const auto blockedButton = builder.addButton({
		.id = u"ayu/hideFromBlocked"_q,
		.title = tr::ayu_FiltersHideFromBlocked(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->hideFromBlocked()),
	});
	if (blockedButton) {
		blockedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->hideFromBlocked());
		}) | on_next([=](bool enabled) {
			AyuSettings::getInstance().setHideFromBlocked(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, blockedButton->lifetime());
	}

	builder.addSkip();
}

void BuildShared(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/sharedFilters"_q,
		.title = tr::ayu_RegexFiltersShared(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->showSettings(AyuFiltersList::Id());
		},
	});
}

void BuildShadowBan(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addButton({
		.id = u"ayu/shadowBanIds"_q,
		.altIds = { u"ayu/shadowBanList"_q },
		.title = tr::ayu_FiltersShadowBan(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->shadowBan = true;
			controller->showSettings(AyuFiltersList::Id());
		},
	});
}

void BuildPerDialog(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			if (!AyuDatabase::hasPerDialogFilters()) {
				return;
			}

			const auto container = wctx.container;
			const auto controller = wctx.controller;

			AddSkip(container);
			AddDivider(container);

			auto ctrl = container->lifetime().make_state<PerDialogFiltersListController>(
				&controller->session(),
				controller);

			auto list = object_ptr<Ui::PaddingWrap<PeerListContent>>(
				container,
				object_ptr<PeerListContent>(
					container,
					ctrl),
				QMargins(0, -st::peerListBox.padding.top(), 0, -st::peerListBox.padding.bottom()));
			AddSkip(container);
			const auto content = container->add(std::move(list));
			AddSkip(container);
			auto delegate = container->lifetime().make_state<PeerListContentDelegateSimple>();
			delegate->setContent(content->entity());
			ctrl->setDelegate(delegate);
		}, [&](const SearchContext &) {
		});
	});
}

QString SmartFilterRuleTypeLabel(Ayu::SmartFilter::AdRuleType type) {
	switch (type) {
	case Ayu::SmartFilter::AdRuleType::Keyword: return u"关键词"_q;
	case Ayu::SmartFilter::AdRuleType::Regex: return u"正则"_q;
	case Ayu::SmartFilter::AdRuleType::Domain: return u"域名"_q;
	case Ayu::SmartFilter::AdRuleType::Channel: return u"频道"_q;
	}
	return u"关键词"_q;
}

void AddSmartFilterAdRuleBox(
		not_null<Ui::GenericBox*> box,
		Fn<void()> onAdded) {
	box->setTitle(rpl::single(u"添加智能广告规则"_q));

	const auto typeGroup = std::make_shared<Ui::RadiobuttonGroup>(
		static_cast<int>(Ayu::SmartFilter::AdRuleType::Keyword));
	const auto addTypeOption = [&](
			Ayu::SmartFilter::AdRuleType type,
			const QString &label) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				typeGroup,
				static_cast<int>(type),
				label,
				st::defaultBoxCheckbox),
			st::settingsCheckboxPadding);
	};
	addTypeOption(Ayu::SmartFilter::AdRuleType::Keyword, u"关键词 (Keyword)"_q);
	addTypeOption(Ayu::SmartFilter::AdRuleType::Regex, u"正则表达式 (Regex)"_q);
	addTypeOption(Ayu::SmartFilter::AdRuleType::Domain, u"域名 (Domain)"_q);
	addTypeOption(Ayu::SmartFilter::AdRuleType::Channel, u"频道 (@Channel)"_q);

	const auto patternField = box->addRow(
		object_ptr<Ui::InputField>(
			box->verticalLayout(),
			st::windowFilterNameInput,
			Ui::InputField::Mode::SingleLine,
			rpl::single(u"规则内容（关键词 / 正则 / 域名 / @频道）"_q)),
		st::markdownLinkFieldPadding);

	const auto caseCheckbox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			u"区分大小写"_q,
			false,
			st::defaultBoxCheckbox),
		st::settingsCheckboxPadding);

	const auto noteField = box->addRow(
		object_ptr<Ui::InputField>(
			box->verticalLayout(),
			st::windowFilterNameInput,
			Ui::InputField::Mode::SingleLine,
			rpl::single(u"备注（可选）"_q)),
		st::markdownLinkFieldPadding);

	const auto saveAndClose = [=] {
		const auto patternText = patternField->getTextWithTags().text.trimmed();
		if (patternText.isEmpty()) {
			patternField->showError();
			return;
		}
		const auto type = static_cast<Ayu::SmartFilter::AdRuleType>(typeGroup->current());
		const auto patternStd = patternText.toStdString();
		const auto caseSensitive = caseCheckbox->checked();
		if (type == Ayu::SmartFilter::AdRuleType::Regex) {
			try {
				auto flags = std::regex::ECMAScript;
				if (!caseSensitive) {
					flags |= std::regex::icase;
				}
				std::regex testRe(patternStd, flags);
			} catch (const std::regex_error &ex) {
				Ui::Toast::Show(u"正则表达式无效: "_q + QString::fromUtf8(ex.what()));
				patternField->showError();
				return;
			}
		}

		auto &engine = Ayu::SmartFilter::SmartFilterEngine::instance();
		auto cfg = engine.config();
		const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		Ayu::SmartFilter::AdRule rule;
		rule.id = "rule_" + std::to_string(stamp) + "_" + std::to_string(cfg.adRules.size());
		rule.enabled = true;
		rule.type = type;
		rule.pattern = patternStd;
		rule.caseSensitive = caseSensitive;
		rule.note = noteField->getTextWithTags().text.trimmed().toStdString();
		cfg.adRules.push_back(std::move(rule));
		engine.setConfig(std::move(cfg));

		if (onAdded) {
			onAdded();
		}
		box->closeBox();
	};

	patternField->submits() | rpl::on_next(saveAndClose, patternField->lifetime());
	box->addButton(tr::lng_settings_save(), saveAndClose);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] {
		patternField->setFocusFast();
	});
}

void BuildSmartFilterSection(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(rpl::single(u"智能广告与表情过滤"_q));

	const auto adSync = std::make_shared<rpl::event_stream<bool>>();
	const auto sponsoredSync = std::make_shared<rpl::event_stream<bool>>();

	const auto adButton = builder.addButton({
		.id = u"ayu/smartFilterAdEnabled"_q,
		.title = rpl::single(u"启用智能广告过滤"_q),
		.st = &st::settingsButtonNoIcon,
		.toggled = adSync->events_starting_with(
			Ayu::SmartFilter::SmartFilterEngine::instance().config().adFilterEnabled),
	});
	if (adButton) {
		adButton->toggledValue(
		) | rpl::filter([](bool enabled) {
			return enabled != Ayu::SmartFilter::SmartFilterEngine::instance().config().adFilterEnabled;
		}) | on_next([](bool enabled) {
			auto &engine = Ayu::SmartFilter::SmartFilterEngine::instance();
			auto cfg = engine.config();
			cfg.adFilterEnabled = enabled;
			engine.setConfig(std::move(cfg));
		}, adButton->lifetime());
	}

	const auto sponsoredButton = builder.addButton({
		.id = u"ayu/smartFilterHideSponsored"_q,
		.title = rpl::single(u"隐藏官方赞助消息 (Sponsored)"_q),
		.st = &st::settingsButtonNoIcon,
		.toggled = sponsoredSync->events_starting_with(
			Ayu::SmartFilter::SmartFilterEngine::instance().config().hideSponsoredMessages),
	});
	if (sponsoredButton) {
		sponsoredButton->toggledValue(
		) | rpl::filter([](bool enabled) {
			return enabled != Ayu::SmartFilter::SmartFilterEngine::instance().config().hideSponsoredMessages;
		}) | on_next([](bool enabled) {
			auto &engine = Ayu::SmartFilter::SmartFilterEngine::instance();
			auto cfg = engine.config();
			cfg.hideSponsoredMessages = enabled;
			engine.setConfig(std::move(cfg));
		}, sponsoredButton->lifetime());
	}

	builder.add([=](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			const auto controller = wctx.controller;

			AddSkip(container);
			AddSubsectionTitle(container, rpl::single(u"广告规则列表"_q));
			const auto rulesContainer = container->add(
				object_ptr<Ui::VerticalLayout>(container));

			const auto rebuildRulesPtr = std::make_shared<Fn<void()>>();
			*rebuildRulesPtr = [=] {
				while (rulesContainer->count() > 0) {
					delete rulesContainer->widgetAt(0);
				}
				const auto cfg = Ayu::SmartFilter::SmartFilterEngine::instance().config();
				for (size_t idx = 0; idx < cfg.adRules.size(); ++idx) {
					const auto &rule = cfg.adRules[idx];
					auto text = u"[%1] %2"_q
						.arg(SmartFilterRuleTypeLabel(rule.type))
						.arg(QString::fromStdString(rule.pattern));
					if (!rule.note.empty()) {
						text += u" (%1)"_q.arg(QString::fromStdString(rule.note));
					}
					if (!rule.enabled) {
						text += u" [已停用]"_q;
					}
					const auto btn = rulesContainer->add(
						object_ptr<Button>(
							rulesContainer,
							rpl::single(text),
							st::settingsButtonNoIcon));
					if (!rule.enabled) {
						btn->setColorOverride(st::windowSubTextFg->c);
					}
					const auto ruleEnabled = rule.enabled;
					btn->setClickedCallback([=] {
						auto *menu = new Ui::PopupMenu(rulesContainer, st::popupMenuWithIcons);
						menu->setAttribute(Qt::WA_DeleteOnClose);
						menu->addAction(
							ruleEnabled ? u"停用规则"_q : u"启用规则"_q,
							[=] {
								auto &engine = Ayu::SmartFilter::SmartFilterEngine::instance();
								auto nextCfg = engine.config();
								if (idx < nextCfg.adRules.size()) {
									nextCfg.adRules[idx].enabled = !nextCfg.adRules[idx].enabled;
									engine.setConfig(std::move(nextCfg));
								}
								crl::on_main(rulesContainer, [=] {
									(*rebuildRulesPtr)();
								});
							},
							ruleEnabled ? &st::menuIconBlock : &st::menuIconUnblock);
						menu->addAction(
							u"删除规则"_q,
							[=] {
								auto &engine = Ayu::SmartFilter::SmartFilterEngine::instance();
								auto nextCfg = engine.config();
								if (idx < nextCfg.adRules.size()) {
									nextCfg.adRules.erase(nextCfg.adRules.begin() + static_cast<std::ptrdiff_t>(idx));
									engine.setConfig(std::move(nextCfg));
								}
								crl::on_main(rulesContainer, [=] {
									(*rebuildRulesPtr)();
								});
							},
							&st::menuIconDelete);
						menu->popup(QCursor::pos());
					});
				}
				if (container->width() > 0) {
					rulesContainer->resizeToWidth(container->width());
				}
			};
			(*rebuildRulesPtr)();

			const auto addRuleBtn = container->add(
				object_ptr<Button>(
					container,
					rpl::single(u"+ 添加广告规则"_q),
					st::settingsButtonNoIcon));
			addRuleBtn->setClickedCallback([=] {
				controller->show(Box(AddSmartFilterAdRuleBox, [=] {
					(*rebuildRulesPtr)();
				}));
			});

			AddSkip(container);
			AddDivider(container);
			AddSkip(container);
			AddSubsectionTitle(container, rpl::single(u"会话白名单（跳过广告过滤）"_q));
			const auto whitelistContainer = container->add(
				object_ptr<Ui::VerticalLayout>(container));

			const auto rebuildWhitelistPtr = std::make_shared<Fn<void()>>();
			*rebuildWhitelistPtr = [=] {
				while (whitelistContainer->count() > 0) {
					delete whitelistContainer->widgetAt(0);
				}
				const auto cfg = Ayu::SmartFilter::SmartFilterEngine::instance().config();
				std::vector<int64_t> ids(
					cfg.adWhitelistChatIds.begin(),
					cfg.adWhitelistChatIds.end());
				std::sort(ids.begin(), ids.end());
				for (const auto chatId : ids) {
					QString label;
					if (const auto peer = controller->session().data().peerLoaded(PeerId(static_cast<BareId>(chatId)))) {
						label = u"%1 (%2)"_q.arg(peer->name()).arg(chatId);
					} else {
						label = u"会话 ID: %1"_q.arg(chatId);
					}
					const auto btn = whitelistContainer->add(
						object_ptr<Button>(
							whitelistContainer,
							rpl::single(label),
							st::settingsButtonNoIcon));
					btn->setClickedCallback([=] {
						auto *menu = new Ui::PopupMenu(whitelistContainer, st::popupMenuWithIcons);
						menu->setAttribute(Qt::WA_DeleteOnClose);
						menu->addAction(
							u"移出白名单"_q,
							[=] {
								Ayu::SmartFilter::SmartFilterEngine::instance().setChatAdWhitelisted(chatId, false);
								crl::on_main(whitelistContainer, [=] {
									(*rebuildWhitelistPtr)();
								});
							},
							&st::menuIconDelete);
						menu->popup(QCursor::pos());
					});
				}
				if (container->width() > 0) {
					whitelistContainer->resizeToWidth(container->width());
				}
			};
			(*rebuildWhitelistPtr)();

			const auto addWhitelistBtn = container->add(
				object_ptr<Button>(
					container,
					rpl::single(u"+ 添加白名单会话"_q),
					st::settingsButtonNoIcon));
			addWhitelistBtn->setClickedCallback([=] {
				auto types = InlineBots::PeerTypes();
				types |= InlineBots::PeerType::User;
				types |= InlineBots::PeerType::Bot;
				types |= InlineBots::PeerType::Group;
				types |= InlineBots::PeerType::Broadcast;
				Window::ShowChooseRecipientBox(
					controller,
					[=](not_null<Data::Thread*> thread) {
						const auto chatId = static_cast<int64_t>(thread->peer()->id.value);
						Ayu::SmartFilter::SmartFilterEngine::instance().setChatAdWhitelisted(chatId, true);
						(*rebuildWhitelistPtr)();
						return true;
					},
					rpl::single(u"选择跳过广告过滤的会话"_q),
					nullptr,
					types);
			});

			AddSkip(container);
			AddDivider(container);
			AddSkip(container);
			AddSubsectionTitle(container, rpl::single(u"智能过滤 JSON 导入 / 导出"_q));

			const auto importJsonBtn = container->add(
				object_ptr<Button>(
					container,
					rpl::single(u"导入智能过滤配置 (JSON)"_q),
					st::settingsButtonNoIcon));
			importJsonBtn->setClickedCallback([=] {
				const auto path = QFileDialog::getOpenFileName(
					Core::App().getFileDialogParent(),
					u"导入智能过滤配置"_q,
					QString(),
					u"JSON Files (*.json);;All Files (*)"_q);
				if (path.isEmpty()) {
					return;
				}
				std::string error;
				if (auto loaded = Ayu::SmartFilter::loadConfigFromFile(path.toStdString(), &error)) {
					const auto adEnabled = loaded->adFilterEnabled;
					const auto hideSponsored = loaded->hideSponsoredMessages;
					Ayu::SmartFilter::SmartFilterEngine::instance().setConfig(std::move(*loaded));
					adSync->fire_copy(adEnabled);
					sponsoredSync->fire_copy(hideSponsored);
					(*rebuildRulesPtr)();
					(*rebuildWhitelistPtr)();
					controller->showToast(u"智能过滤配置已导入"_q);
				} else {
					controller->showToast(u"导入失败: "_q + QString::fromStdString(error));
				}
			});

			const auto exportJsonBtn = container->add(
				object_ptr<Button>(
					container,
					rpl::single(u"导出智能过滤配置 (JSON)"_q),
					st::settingsButtonNoIcon));
			exportJsonBtn->setClickedCallback([=] {
				const auto path = QFileDialog::getSaveFileName(
					Core::App().getFileDialogParent(),
					u"导出智能过滤配置"_q,
					u"ayu_smart_filter.json"_q,
					u"JSON Files (*.json);;All Files (*)"_q);
				if (path.isEmpty()) {
					return;
				}
				std::string error;
				const auto cfg = Ayu::SmartFilter::SmartFilterEngine::instance().config();
				if (Ayu::SmartFilter::saveConfigToFile(path.toStdString(), cfg, &error)) {
					controller->showToast(u"智能过滤配置已导出"_q);
				} else {
					controller->showToast(u"导出失败: "_q + QString::fromStdString(error));
				}
			});
			AddSkip(container);
		}, [&](const SearchContext &) {
		});
	});
}

const auto kMeta = BuildHelper({
	.id = AyuFilters::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryFilters,
	.icon = &st::menuIconTagFilter,
}, [](SectionBuilder &builder) {
	BuildFiltersSettings(builder);
	BuildShared(builder);
	BuildShadowBan(builder);
	BuildPerDialog(builder);
	BuildSmartFilterSection(builder);
});

} // namespace

rpl::producer<QString> AyuFilters::title() {
	return tr::ayu_CategoryFilters();
}

void AyuFilters::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	addAction(
		tr::ayu_FiltersMenuSelectChat(tr::now),
		[=] {
			if (const auto window = Core::App().activeWindow()) {
				if (const auto controller = window->sessionController()) {
					auto types = InlineBots::PeerTypes();
					types |= InlineBots::PeerType::Bot;
					types |= InlineBots::PeerType::Group;
					types |= InlineBots::PeerType::Broadcast;

					Window::ShowChooseRecipientBox(
						controller,
						[=](not_null<Data::Thread*> thread) {
							const auto peer = thread->peer();
							controller->dialogId = getDialogIdFromPeer(peer);
							controller->showExclude = true;
							controller->showSettings(AyuFiltersList::Id());
							return true;
						},
						tr::ayu_FiltersMenuSelectChat(),
						nullptr,
						types);
				}
			}
		},
		&st::menuIconSearch);
	addAction({ .isSeparator = true });
	addAction(
		tr::ayu_FiltersMenuImport(tr::now),
		[=] {
			auto box = Box(Ui::FillImportFiltersBox, true);
			Ui::show(std::move(box));
		},
		&st::menuIconArchive);
	if (AyuDatabase::hasFilters()) {
		addAction(
			tr::ayu_FiltersMenuExport(tr::now),
			[=] {
				auto box = Box(Ui::FillImportFiltersBox, false);
				Ui::show(std::move(box));
			},
			&st::menuIconUnarchive);
	}
	addAction({ .isSeparator = true });
	addAction({
		.text = tr::ayu_FiltersMenuClear(tr::now),
		.handler = [=] {
			auto callback = [=](Fn<void()> &&close) {
				AyuDatabase::deleteAllFilters();
				AyuDatabase::deleteAllExclusions();
				FiltersCacheController::rebuildCache();
				FiltersCacheController::fireUpdate();
				close();
			};
			auto box = Ui::MakeConfirmBox({
				.text = tr::ayu_FiltersClearPopupText(),
				.confirmed = callback,
				.confirmText = tr::ayu_FiltersClearPopupActionText(),
				.confirmStyle = &st::attentionBoxButton,
			});
			Ui::show(std::move(box));
		},
		.icon = &st::menuIconClearAttention,
		.isAttention = true,
	});
}

AyuFilters::AyuFilters(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuFilters::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuFiltersId() {
	return AyuFilters::Id();
}

} // namespace Settings
