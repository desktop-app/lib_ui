// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/box_content_divider.h"

#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>

namespace Ui {

BoxContentDivider::BoxContentDivider(
	QWidget *parent,
	int height,
	const style::DividerBar &st,
	RectParts parts)
: RpWidget(parent)
, _st(st)
, _parts(parts) {
	resize(width(), height);
}

void BoxContentDivider::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.fillRect(e->rect(), _st.bg);
	if (_parts & RectPart::Top) {
		paintTop(p);
	}
	if (_parts & RectPart::Bottom) {
		paintBottom(p);
	}
}

void BoxContentDivider::paintTop(QPainter &p, int skip) {
	const auto dividerFillTop = QRect(
		0,
		skip,
		width(),
		_st.top.height());
	_st.top.fill(p, dividerFillTop);
}

void BoxContentDivider::paintBottom(QPainter &p, int skip) {
	const auto dividerFillBottom = myrtlrect(
		0,
		height() - skip - _st.bottom.height(),
		width(),
		_st.bottom.height());
	_st.bottom.fill(p, dividerFillBottom);
}

const style::color &BoxContentDivider::color() const {
	return _st.bg;
}

} // namespace Ui
