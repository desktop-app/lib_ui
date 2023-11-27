// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_add_action_callback.h"

#include "ui/style/style_core.h"

namespace Ui::Menu {

MenuCallback::MenuCallback(MenuCallback::Callback callback)
: _callback(std::move(callback)) {
}

QAction *MenuCallback::operator()(Args &&args) const {
	return _callback(std::move(args));
}

QAction *MenuCallback::operator()(
		const QString &text,
		Fn<void()> handler,
		const style::icon *icon) const {
	return _callback({ text, std::move(handler), icon, nullptr });
}

} // namespace Ui::Menu
