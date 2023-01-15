// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtCore/QRect>

namespace rect {

// Margins.
namespace m {
namespace sum {

/*
	Horizontal.
	rect::m::sum::h(mypadding);
*/
[[nodiscard]] int h(const QMargins &margins) {
	return margins.left() + margins.right();
}

[[nodiscard]] float64 h(const QMarginsF &margins) {
	return margins.left() + margins.right();
}

/*
	Vertical.
	rect::m::sum::v(mypadding);
*/
[[nodiscard]] int v(const QMargins &margins) {
	return margins.top() + margins.bottom();
}

[[nodiscard]] float64 v(const QMarginsF &margins) {
	return margins.top() + margins.bottom();
}

} // namespace sum
} // namespace m

/*
	rect::right(mywidget);
*/
[[nodiscard]] int right(const QRect &r) {
	return r.left() + r.width();
}

[[nodiscard]] float64 right(const QRectF &r) {
	return r.left() + r.width();
}

[[nodiscard]] int right(not_null<QWidget*> w) {
	return w->x() + w->width();
}

/*
	rect::bottom(mywidget);
*/
[[nodiscard]] int bottom(const QRect &r) {
	return r.top() + r.height();
}

[[nodiscard]] float64 bottom(const QRectF &r) {
	return r.top() + r.height();
}

[[nodiscard]] int bottom(not_null<const QWidget*> w) {
	return w->y() + w->height();
}

} // namespace rect

/*
	Rect(mysize);
*/
[[nodiscard]] QRect Rect(const QSize &s) {
	return QRect(QPoint(), s);
}

[[nodiscard]] QRectF Rect(const QSizeF &s) {
	return QRectF(QPointF(), s);
}

/*
	Size(myside);
*/
[[nodiscard]] QSize Size(int side) {
	return QSize(side, side);
}

[[nodiscard]] QSizeF Size(float64 side) {
	return QSizeF(side, side);
}

/*
	Margins(myvalue);
*/
[[nodiscard]] QMargins Margins(int side) {
	return QMargins{ side, side, side, side };
}

[[nodiscard]] QMarginsF Margins(float64 side) {
	return QMarginsF{ side, side, side, side };
}
