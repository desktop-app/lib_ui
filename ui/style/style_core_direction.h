// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core_types.h"

#include <QtCore/QPoint>
#include <QtCore/QRect>

namespace style {

[[nodiscard]] bool RightToLeft();
void SetRightToLeft(bool rtl);

[[nodiscard]] inline Qt::LayoutDirection LayoutDirection() {
	return RightToLeft() ? Qt::RightToLeft : Qt::LeftToRight;
}

inline QRect centerrect(const QRect &inRect, const QRect &rect) {
	return QRect(
		inRect.x() + (inRect.width() - rect.width()) / 2,
		inRect.y() + (inRect.height() - rect.height()) / 2,
		rect.width(),
		rect.height());
}

inline QRect centerrect(const QRect &inRect, const style::icon &icon) {
	return centerrect(inRect, QRect(0, 0, icon.width(), icon.height()));
}

inline QPoint rtlpoint(int x, int y, int outerw) {
	return QPoint(RightToLeft() ? (outerw - x) : x, y);
}

inline QPoint rtlpoint(const QPoint &p, int outerw) {
	return RightToLeft() ? QPoint(outerw - p.x(), p.y()) : p;
}

inline QPointF rtlpoint(const QPointF &p, int outerw) {
	return RightToLeft() ? QPointF(outerw - p.x(), p.y()) : p;
}

inline QRect rtlrect(int x, int y, int w, int h, int outerw) {
	return QRect(RightToLeft() ? (outerw - x - w) : x, y, w, h);
}

inline QRect rtlrect(const QRect &r, int outerw) {
	return RightToLeft()
		? QRect(outerw - r.x() - r.width(), r.y(), r.width(), r.height())
		: r;
}

inline QRectF rtlrect(const QRectF &r, int outerw) {
	return RightToLeft()
		? QRectF(outerw - r.x() - r.width(), r.y(), r.width(), r.height())
		: r;
}

} // namespace style

struct TextWithEntities;

namespace st {

[[nodiscard]] inline bool rtl() {
	return style::RightToLeft();
}

[[nodiscard]] QString wrap_rtl(const QString &text);
[[nodiscard]] TextWithEntities wrap_rtl(const TextWithEntities &text);

} // namespace st
