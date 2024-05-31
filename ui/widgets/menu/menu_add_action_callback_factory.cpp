// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_add_action_callback_factory.h"

#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_widgets.h"

namespace Ui::Menu {

MenuCallback CreateAddActionCallback(not_null<Ui::PopupMenu*> menu) {
	return MenuCallback([=](MenuCallback::Args a) -> QAction* {
		const auto initFilter = [&](not_null<Ui::Menu::Action*> action) {
			if (const auto copy = a.triggerFilter) {
				action->setClickedCallback([=] {
					const auto weak = Ui::MakeWeak(action);
					if (copy() && weak && !action->isDisabled()) {
						action->setDisabled(true);
						crl::on_main(
							weak,
							[=] { action->setDisabled(false); });
					}
				});
			}
		};
		if (a.hideRequests) {
			std::move(
				a.hideRequests
			) | rpl::start_with_next([=](anim::type animated) {
				menu->hideMenu(animated == anim::type::instant);
			}, menu->lifetime());
		}
		if (a.addTopShift) {
			menu->setTopShift(a.addTopShift);
			return nullptr;
		} else if (a.fillSubmenu) {
			const auto action = menu->addAction(
				a.text,
				std::move(a.handler),
				a.icon);
			// Dummy menu.
			action->setMenu(Ui::CreateChild<QMenu>(menu->menu().get()));
			a.fillSubmenu(menu->ensureSubmenu(action, menu->st()));
			return action;
		} else if (a.separatorSt || a.isSeparator) {
			return menu->addSeparator(a.separatorSt);
		} else if (a.isAttention) {
			auto owned = base::make_unique_q<Ui::Menu::Action>(
				menu,
				st::menuWithIconsAttention,
				Ui::Menu::CreateAction(
					menu->menu().get(),
					a.text,
					std::move(a.handler)),
				a.icon,
				a.icon);
			initFilter(owned.get());
			return menu->addAction(std::move(owned));
		} else if (a.triggerFilter) {
			auto owned = base::make_unique_q<Ui::Menu::Action>(
				menu,
				menu->st().menu,
				Ui::Menu::CreateAction(
					menu->menu().get(),
					a.text,
					std::move(a.handler)),
				a.icon,
				a.icon);
			initFilter(owned.get());
			return menu->addAction(std::move(owned));
		}
		return menu->addAction(a.text, std::move(a.handler), a.icon);
	});
}

MenuCallback CreateAddActionCallback(
		const base::unique_qptr<Ui::PopupMenu> &menu) {
	return CreateAddActionCallback(menu.get());
}

} // namespace Ui::Menu
