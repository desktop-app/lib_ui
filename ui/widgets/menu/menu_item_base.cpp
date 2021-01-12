// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_item_base.h"

namespace Ui::Menu {

ItemBase::ItemBase(
	not_null<RpWidget*> parent,
	const style::Menu &st, int index)
: RippleButton(parent, st.ripple)
, _index(index) {
	init();
}

void ItemBase::setSelected(
		bool selected,
		TriggeredSource source) {
	if (!isEnabled()) {
		return;
	}
	if (_selected.current() != selected) {
		_lastTriggeredSource = source;
		_selected = selected;
		update();
	}
}

bool ItemBase::isSelected() const {
	return _selected.current();
}
rpl::producer<bool> ItemBase::selects() const {
	return _selected.changes();
}

TriggeredSource ItemBase::lastTriggeredSource() const {
	return _lastTriggeredSource;
}

int ItemBase::index() const {
	return _index;
}

void ItemBase::setClicked(TriggeredSource source) {
	if (isEnabled()) {
		_lastTriggeredSource = source;
		_clicks.fire({});
	}
}

rpl::producer<> ItemBase::clicks() const {
	return rpl::merge(
		AbstractButton::clicks() | rpl::to_empty,
		_clicks.events());
}

rpl::producer<int> ItemBase::contentWidthValue() const {
	return _contentWidth.value();
}

int ItemBase::contentWidth() const {
	return _contentWidth.current();
}

bool ItemBase::hasSubmenu() const {
	return _hasSubmenu;
}

void ItemBase::setHasSubmenu(bool value) {
	_hasSubmenu = value;
}

void ItemBase::init() {
	events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isEnabled()
			&& isSelected()
			&& (e->type() == QEvent::MouseButtonRelease);
	}) | rpl::to_empty | rpl::start_with_next([=] {
		const auto point = mapFromGlobal(QCursor::pos());
		if (!rect().contains(point)) {
			setSelected(false);
		}
	}, lifetime());
}

void ItemBase::initResizeHook(rpl::producer<QSize> &&size) {
	std::move(
		size
	) | rpl::start_with_next([=](QSize s) {
		resize(s.width(), contentHeight());
	}, lifetime());
}

void ItemBase::setContentWidth(int w) {
	_contentWidth = w;
}

} // namespace Ui::Menu
