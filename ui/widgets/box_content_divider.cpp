// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/box_content_divider.h"

#include "styles/style_layers.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>

namespace Ui {

BoxContentDivider::BoxContentDivider(QWidget *parent)
: BoxContentDivider(parent, st::boxDividerHeight) {
}

BoxContentDivider::BoxContentDivider(QWidget *parent, int height)
: BoxContentDivider(parent, height, st::boxDividerBg) {
}

BoxContentDivider::BoxContentDivider(
	QWidget *parent,
	int height,
	const style::color &bg,
	RectParts parts)
: RpWidget(parent)
, _bg(bg)
, _parts(parts) {
	resize(width(), height);
}

void BoxContentDivider::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.fillRect(e->rect(), _bg);
	if (_parts & RectPart::Top) {
		paintTop(p);
	}
	if (_parts & RectPart::Bottom) {
		paintBottom(p);
	}
}

void BoxContentDivider::paintTop(QPainter &p) {
	const auto dividerFillTop = QRect(
		0,
		0,
		width(),
		st::boxDividerTop.height());
	st::boxDividerTop.fill(p, dividerFillTop);
}

void BoxContentDivider::paintBottom(QPainter &p) {
	const auto dividerFillBottom = myrtlrect(
		0,
		height() - st::boxDividerBottom.height(),
		width(),
		st::boxDividerBottom.height());
	st::boxDividerBottom.fill(p, dividerFillBottom);
}

const style::color &BoxContentDivider::color() const {
	return _bg;
}

} // namespace Ui
