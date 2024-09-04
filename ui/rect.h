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
[[nodiscard]] inline int h(const QMargins &margins) {
	return margins.left() + margins.right();
}

[[nodiscard]] inline float64 h(const QMarginsF &margins) {
	return margins.left() + margins.right();
}

/*
	Vertical.
	rect::m::sum::v(mypadding);
*/
[[nodiscard]] inline int v(const QMargins &margins) {
	return margins.top() + margins.bottom();
}

[[nodiscard]] inline float64 v(const QMarginsF &margins) {
	return margins.top() + margins.bottom();
}

} // namespace sum

namespace pos {

/*
	Top-left.
	rect::m::pos::tl(mypadding);
*/
[[nodiscard]] inline QPoint tl(const QMargins &margins) {
	return { margins.left(), margins.top() };
}

[[nodiscard]] inline QPointF tl(const QMarginsF &margins) {
	return { margins.left(), margins.top() };
}

} // namespace pos
} // namespace m

/*
	rect::right(mywidget);
*/
[[nodiscard]] inline int right(const QRect &r) {
	return r.left() + r.width();
}

[[nodiscard]] inline float64 right(const QRectF &r) {
	return r.left() + r.width();
}

[[nodiscard]] inline int right(not_null<QWidget*> w) {
	return w->x() + w->width();
}

/*
	rect::bottom(mywidget);
*/
[[nodiscard]] inline int bottom(const QRect &r) {
	return r.top() + r.height();
}

[[nodiscard]] inline float64 bottom(const QRectF &r) {
	return r.top() + r.height();
}

[[nodiscard]] inline int bottom(not_null<const QWidget*> w) {
	return w->y() + w->height();
}

/*
	rect::center(mywidget);
*/
[[nodiscard]] inline QPoint center(const QRect &r) {
	return { r.left() + r.width() / 2, r.top() + r.height() / 2 };
}

[[nodiscard]] inline QPointF center(const QRectF &r) {
	return { r.left() + r.width() / 2, r.top() + r.height() / 2 };
}

[[nodiscard]] inline QPoint center(not_null<const QWidget*> w) {
	return { w->x() + w->width() / 2, w->y() + w->height() / 2 };
}

} // namespace rect

/*
	Rect(mysize);
*/
[[nodiscard]] inline QRect Rect(const QSize &s) {
	return QRect(QPoint(), s);
}

[[nodiscard]] inline QRectF Rect(const QSizeF &s) {
	return QRectF(QPointF(), s);
}

/*
	Rect(x, y, mysize);
*/
[[nodiscard]] inline QRect Rect(int x, int y, const QSize &s) {
	return QRect(QPoint(x, y), s);
}

[[nodiscard]] inline QRectF Rect(float64 x, float64 y, const QSizeF &s) {
	return QRectF(QPointF(x, y), s);
}

/*
	Size(myside);
*/
[[nodiscard]] inline QSize Size(int side) {
	return QSize(side, side);
}

[[nodiscard]] inline QSizeF Size(float64 side) {
	return QSizeF(side, side);
}

/*
	Margins(myvalue);
*/
[[nodiscard]] inline QMargins Margins(int side) {
	return QMargins{ side, side, side, side };
}

[[nodiscard]] inline QMarginsF Margins(float64 side) {
	return QMarginsF{ side, side, side, side };
}
