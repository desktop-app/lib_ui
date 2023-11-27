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
		} else if (a.isSeparator) {
			return menu->addSeparator();
		} else if (a.isAttention) {
			return menu->addAction(base::make_unique_q<Ui::Menu::Action>(
				menu,
				st::menuWithIconsAttention,
				Ui::Menu::CreateAction(
					menu->menu().get(),
					a.text,
					std::move(a.handler)),
				a.icon,
				a.icon));
		}
		return menu->addAction(a.text, std::move(a.handler), a.icon);
	});
}

MenuCallback CreateAddActionCallback(
		const base::unique_qptr<Ui::PopupMenu> &menu) {
	return CreateAddActionCallback(menu.get());
}

} // namespace Ui::Menu
