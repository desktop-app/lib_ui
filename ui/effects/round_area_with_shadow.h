// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Ui {

struct ImageSubrect {
	not_null<QImage*> image;
	QRect rect;
};

class RoundAreaWithShadow final {
public:
	static constexpr auto kFramesCount = 32;

	[[nodiscard]] static QImage PrepareImage(QSize size);
	[[nodiscard]] static QImage PrepareFramesCache(
		QSize frame,
		int columns = 1);
	[[nodiscard]] static QRect FrameCacheRect(
		int frameIndex,
		int column,
		QSize frame);

	// Returns center area which could be just filled with a solid color.
	static QRect FillWithImage(
		QPainter &p,
		QRect geometry,
		const ImageSubrect &pattern);

	RoundAreaWithShadow(QSize inner, QMargins shadow, int twiceRadiusMax);

	void setBackgroundColor(const QColor &background);
	void setShadowColor(const QColor &shadow);

	[[nodiscard]] ImageSubrect validateFrame(
		int frameIndex,
		float64 scale,
		float64 radius);
	[[nodiscard]] ImageSubrect validateOverlayMask(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		int twiceRadius,
		float64 scale);
	[[nodiscard]] ImageSubrect validateOverlayShadow(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		int twiceRadius,
		float64 scale,
		const ImageSubrect &mask);

	void overlayExpandedBorder(
		QPainter &p,
		QSize size,
		float64 expandRatio,
		float64 radiusFrom,
		float64 radiusTill,
		float64 scale);

private:
	[[nodiscard]] QRect validateShadow(
		int frameIndex,
		float64 scale,
		float64 radius);

	QRect _inner;
	QSize _outer;
	QSize _overlay;

	std::array<bool, kFramesCount> _validBg = { { false } };
	std::array<bool, kFramesCount> _validShadow = { { false } };
	std::array<bool, kFramesCount> _validOverlayMask = { { false } };
	std::array<bool, kFramesCount> _validOverlayShadow = { { false } };
	QColor _background;
	QColor _gradient;
	QColor _shadow;
	QImage _cacheBg;
	QImage _shadowParts;
	QImage _overlayCacheParts;
	QImage _overlayMaskScaled;
	QImage _overlayShadowScaled;
	QImage _shadowBuffer;

};

} // namespace Ui
