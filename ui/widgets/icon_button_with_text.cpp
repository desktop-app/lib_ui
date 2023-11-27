// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/icon_button_with_text.h"

namespace Ui {

IconButtonWithText::IconButtonWithText(
	not_null<RpWidget*> parent,
	const style::IconButtonWithText &st)
: IconButton(parent, st.iconButton)
, _st(st) {
}

void IconButtonWithText::paintEvent(QPaintEvent *e) {
	IconButton::paintEvent(e);

	const auto r = rect() - _st.textPadding;
	const auto overIconOpacity = IconButton::iconOverOpacity();

	auto p = QPainter(this);
	p.setFont(_st.font);
	p.setPen((overIconOpacity == 1.) ? _st.textFgOver : _st.textFg);
	p.drawText(r, _text, _st.textAlign);

	if (overIconOpacity > 0. && overIconOpacity < 1.) {
		p.setPen(_st.textFgOver);
		p.setOpacity(overIconOpacity);
		p.drawText(r, _text, _st.textAlign);
	}
}

void IconButtonWithText::setText(const QString &text) {
	if (_text != text) {
		_text = text;
		update();
	}
}

} // namespace Ui
