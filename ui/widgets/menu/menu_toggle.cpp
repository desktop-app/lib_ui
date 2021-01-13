// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_toggle.h"

#include "ui/widgets/checkbox.h"

namespace Ui::Menu {

Toggle::Toggle(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	const QString &text,
	Fn<void()> &&callback,
	const style::icon *icon,
	const style::icon *iconOver)
: Action(
	parent,
	st,
	CreateAction(parent, text, std::move(callback)),
	icon,
	iconOver)
, _padding(st.itemPadding)
, _toggleShift(st.itemToggleShift)
, _itemToggle(st.itemToggle)
, _itemToggleOver(st.itemToggleOver) {

	const auto processAction = [=] {
		if (!action()->isCheckable()) {
			_toggle.reset();
			return;
		}
		if (_toggle) {
			_toggle->setChecked(action()->isChecked(), anim::type::normal);
		} else {
			_toggle = std::make_unique<ToggleView>(
				st.itemToggle,
				action()->isChecked(),
				[=] { update(); });
		}
	};
	processAction();
	connect(action(), &QAction::changed, [=] { processAction(); });

	selects(
	) | rpl::start_with_next([=](const CallbackData &data) {
		if (!_toggle) {
			return;
		}
		_toggle->setStyle(data.selected ? _itemToggleOver : _itemToggle);
	}, lifetime());

}

void Toggle::paintEvent(QPaintEvent *e) {
	Action::paintEvent(e);
	if (_toggle) {
		Painter p(this);
		const auto toggleSize = _toggle->getSize();
		_toggle->paint(
			p,
			width() - _padding.right() - toggleSize.width() + _toggleShift,
			(contentHeight() - toggleSize.height()) / 2, width());
	}
}

void Toggle::finishAnimating() {
	ItemBase::finishAnimating();
	if (_toggle) {
		_toggle->finishAnimating();
	}
}

} // namespace Ui::Menu
