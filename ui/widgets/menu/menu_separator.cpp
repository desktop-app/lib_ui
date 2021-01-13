// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_separator.h"

#include "ui/painter.h"

namespace Ui::Menu {

Separator::Separator(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	not_null<QAction*> action)
: ItemBase(parent, st)
, _lineWidth(st.separatorWidth)
, _padding(st.separatorPadding)
, _fg(st.separatorFg)
, _bg(st.itemBg)
, _height(_padding.top() + _lineWidth + _padding.bottom())
, _action(action) {

	initResizeHook(parent->sizeValue());
	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		p.fillRect(0, 0, width(), _height, _bg);
		p.fillRect(
			_padding.left(),
			_padding.top(),
			width() - _padding.left() - _padding.right(),
			_lineWidth,
			_fg);
	}, lifetime());
}

not_null<QAction*> Separator::action() const {
	return _action;
}

bool Separator::isEnabled() const {
	return false;
}

int Separator::contentHeight() const {
	return _height;
}

} // namespace Ui::Menu
