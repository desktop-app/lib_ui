// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/side_bar_button.h"

#include "ui/effects/ripple_animation.h"

#include <QtGui/QtEvents>

namespace Ui {
namespace {

constexpr auto kMaxLabelLines = 3;

} // namespace

SideBarButton::SideBarButton(
	not_null<QWidget*> parent,
	const QString &title,
	const style::SideBarButton &st)
: RippleButton(parent, st.ripple)
, _st(st)
, _text(_st.minTextWidth) {
	_text.setText(_st.style, title);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void SideBarButton::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_active = active;
	update();
}

void SideBarButton::setBadge(const QString &badge) {
	if (_badge.toString() == badge) {
		return;
	}
	_badge.setText(_st.badgeStyle, badge);
	update();
}

int SideBarButton::resizeGetHeight(int newWidth) {
	auto result = _st.minHeight;
	const auto text = _text.countHeight(newWidth - _st.textSkip * 2);
	const auto add = text - _st.style.font->height;
	return result + std::max(add, 0);
}

void SideBarButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();

	p.fillRect(clip, _active ? _st.textBgActive : _st.textBg);

	RippleButton::paintRipple(p, 0, 0);

	const auto &icon = _active ? _st.iconActive : _st.icon;
	const auto x = (_st.iconPosition.x() < 0)
		? (width() - icon.width()) / 2
		: _st.iconPosition.x();
	const auto y = (_st.iconPosition.y() < 0)
		? (height() - icon.height()) / 2
		: _st.iconPosition.y();
	icon.paint(p, x, y, width());
	p.setPen(_active ? _st.textFgActive : _st.textFg);
	_text.drawElided(
		p,
		_st.textSkip,
		_st.textTop,
		(width() - 2 * _st.textSkip),
		kMaxLabelLines,
		style::al_top);
}

} // namespace Ui
