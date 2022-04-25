// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_common.h"

#include "ui/ui_utility.h"
#include "base/invoke_queued.h"

#include <QAction>

namespace Ui::Menu {

not_null<QAction*> CreateAction(
		QWidget *parent,
		const QString &text,
		Fn<void()> &&callback) {
	const auto action = new QAction(text, parent);
	const auto guard = MakeWeak(parent);
	auto triggered = [guard, callback = std::move(callback)]() mutable {
		InvokeQueued(guard, std::move(callback));
	};
	parent->connect(action, &QAction::triggered, std::move(triggered));
	return action;
}

} // namespace Ui::Menu
