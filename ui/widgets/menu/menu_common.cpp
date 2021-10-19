// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_common.h"

#include <QAction>

namespace Ui::Menu {

not_null<QAction*> CreateAction(
		QWidget *parent,
		const QString &text,
		Fn<void()> &&callback) {
	const auto action = new QAction(text, parent);
	parent->connect(
		action,
		&QAction::triggered,
		action,
		std::move(callback),
		Qt::QueuedConnection);
	return action;
}

} // namespace Ui::Menu
