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
	const style::color &bg)
: RpWidget(parent)
, _bg(bg) {
	resize(width(), height);
}

void BoxContentDivider::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.fillRect(e->rect(), _bg);
	const auto dividerFillTop = QRect(
		0,
		0,
		width(),
		st::boxDividerTop.height());
	st::boxDividerTop.fill(p, dividerFillTop);
	const auto dividerFillBottom = myrtlrect(
		0,
		height() - st::boxDividerBottom.height(),
		width(),
		st::boxDividerBottom.height());
	st::boxDividerBottom.fill(p, dividerFillBottom);
}

} // namespace Ui
