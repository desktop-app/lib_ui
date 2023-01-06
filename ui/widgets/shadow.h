// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "ui/rect_part.h"

namespace style {
struct Shadow;
} // namespace style

namespace Ui {

class PlainShadow : public RpWidget {
public:
	PlainShadow(QWidget *parent);
	PlainShadow(QWidget *parent, style::color color);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	style::color _color;

};

class Shadow : public TWidget {
public:
	Shadow(
		QWidget *parent,
		const style::Shadow &st,
		RectParts sides = RectPart::AllSides)
	: TWidget(parent)
	, _st(st)
	, _sides(sides) {
	}

	static void paint(
		QPainter &p,
		const QRect &box,
		int outerWidth,
		const style::Shadow &st,
		RectParts sides = RectPart::AllSides);

	static void paint(
		QPainter &p,
		const QRect &box,
		int outerWidth,
		const style::Shadow &st,
		const std::array<QImage, 4> &corners,
		RectParts sides = RectPart::AllSides);

	static void paint(
		QPainter &p,
		const QRect &box,
		int outerWidth,
		const style::Shadow &st,
		const std::array<QImage, 4> &sides,
		const std::array<QImage, 4> &corners);

	static QPixmap grab(
		not_null<TWidget*> target,
		const style::Shadow &shadow,
		RectParts sides = RectPart::AllSides);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::Shadow &_st;
	RectParts _sides;

};

} // namespace Ui
