// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "styles/style_widgets.h"

class Painter;

namespace Ui::Menu {

class Action : public ItemBase {
public:
	Action(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		not_null<QAction*> action,
		const style::icon *icon,
		const style::icon *iconOver);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

	void setIcon(
		const style::icon *icon,
		const style::icon *iconOver = nullptr);

protected:
	void paintEvent(QPaintEvent *e) override;
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

	void paintBackground(QPainter &p, bool selected);
	void paintText(Painter &p);

private:
	void processAction();
	void paint(Painter &p);

	bool hasSubmenu() const;

	Text::String _text;
	QString _shortcut;
	const not_null<QAction*> _action;
	const style::Menu &_st;
	const style::icon *_icon;
	const style::icon *_iconOver;
//	std::unique_ptr<ToggleView> _toggle;
	int _textWidth = 0;
	const int _height;

};

} // namespace Ui::Menu
