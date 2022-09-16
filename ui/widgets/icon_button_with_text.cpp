/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
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

	auto p = QPainter(this);
	p.setFont(_st.font);
	p.setPen(_st.textFg);
	p.drawText(r, _text, _st.textAlign);

	const auto overIconOpacity = IconButton::iconOverOpacity();
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
