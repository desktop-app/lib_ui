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
struct BoxShadow;
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

class Shadow : public RpWidget {
public:
	Shadow(
		QWidget *parent,
		const style::Shadow &st,
		RectParts sides = RectPart::AllSides)
	: RpWidget(parent)
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
		not_null<RpWidget*> target,
		const style::Shadow &shadow,
		RectParts sides = RectPart::AllSides);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::Shadow &_st;
	RectParts _sides;

};

// Generates and paints box shadows programmatically using Gaussian blur.
// No pre-drawn sprite assets required — the shadow is computed at runtime
// and cached as a 9-grid image for efficient painting at any size.
class BoxShadow final {
public:
	explicit BoxShadow(const style::BoxShadow &st);

	[[nodiscard]] static QMargins ExtendFor(const style::BoxShadow &st);

	[[nodiscard]] QMargins extend() const;
	[[nodiscard]] float64 opacity() const;

	void paint(
		QPainter &p,
		const QRect &box,
		int cornerRadius) const;
	void paint(
		QPainter &p,
		const QRect &box,
		int cornerRadius,
		float64 opacity) const;

private:
	friend class RoundShadowAnimation;

	struct Grid {
		QImage image;
		int cornerL = 0;
		int cornerT = 0;
		int cornerR = 0;
		int cornerB = 0;
		int middle = 0;
	};
	[[nodiscard]] Grid preparedGrid(int cornerRadius) const;

	void prepare(int cornerRadius) const;

	int _blurRadius = 0;
	QPoint _offset;
	float64 _opacity = 1.;
	mutable int _cornerRadius = -1;
	mutable QImage _cache;
	mutable int _cornerL = 0;
	mutable int _cornerT = 0;
	mutable int _cornerR = 0;
	mutable int _cornerB = 0;
	mutable int _middle = 0;

};

} // namespace Ui
