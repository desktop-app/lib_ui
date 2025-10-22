// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/menu/menu_item_base.h"

class Painter;

namespace style {
struct Menu;
struct MenuSeparator;
} // namespace style

namespace Ui::Menu {

class Separator : public ItemBase {
public:
	Separator(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		const style::MenuSeparator &separator,
		not_null<QAction*> action);

	QAccessible::Role accessibilityRole() override {
		return QAccessible::Role::Separator;
	}

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

protected:
	int contentHeight() const override;

private:
	const int _lineWidth;
	const style::margins &_padding;
	const style::color &_fg;
	const style::color &_bg;
	const int _height;
	const not_null<QAction*> _action;

};

} // namespace Ui::Menu
