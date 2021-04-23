// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/menu/menu_action.h"
#include "styles/style_widgets.h"

namespace Ui {
class ToggleView;
} // namespace Ui

namespace Ui::Menu {

class Toggle : public Action {
public:
	Toggle(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		const QString &text,
		Fn<void()> &&callback,
		const style::icon *icon,
		const style::icon *iconOver);
	~Toggle();

	void finishAnimating() override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::margins &_padding;
	const int _toggleShift;
	const style::Toggle &_itemToggle;
	const style::Toggle &_itemToggleOver;
	std::unique_ptr<ToggleView> _toggle;

};

} // namespace Ui::Menu
