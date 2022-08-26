// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/round_area_with_shadow.h"

#include "ui/style/style_core.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"

namespace Ui {
namespace {

constexpr auto kBgCacheIndex = 0;
constexpr auto kShadowCacheIndex = 0;
constexpr auto kOverlayMaskCacheIndex = 0;
constexpr auto kOverlayShadowCacheIndex = 1;
constexpr auto kOverlayCacheColumsCount = 2;
constexpr auto kDivider = 4;

} // namespace

[[nodiscard]] QImage RoundAreaWithShadow::PrepareImage(QSize size) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		size * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] QImage RoundAreaWithShadow::PrepareFramesCache(
		QSize frame,
		int columns) {
	static_assert(!(kFramesCount % kDivider));

	return PrepareImage(QSize(
		frame.width() * kDivider * columns,
		frame.height() * kFramesCount / kDivider));
}

[[nodiscard]] QRect RoundAreaWithShadow::FrameCacheRect(
		int frameIndex,
		int column,
		QSize frame) {
	const auto ratio = style::DevicePixelRatio();
	const auto origin = QPoint(
		frame.width() * (kDivider * column + (frameIndex % kDivider)),
		frame.height() * (frameIndex / kDivider));
	return QRect(ratio * origin, ratio * frame);
}

RoundAreaWithShadow::RoundAreaWithShadow(
	QSize inner,
	QMargins shadow,
	int twiceRadiusMax)
: _inner({}, inner)
, _outer(_inner.marginsAdded(shadow).size())
, _overlay(QRect(
	0,
	0,
	std::max(inner.width(), twiceRadiusMax),
	std::max(inner.height(), twiceRadiusMax)).marginsAdded(shadow).size())
, _cacheBg(PrepareFramesCache(_outer))
, _shadowParts(PrepareFramesCache(_outer))
, _overlayCacheParts(PrepareFramesCache(_overlay, kOverlayCacheColumsCount))
, _overlayMaskScaled(PrepareImage(_overlay))
, _overlayShadowScaled(PrepareImage(_overlay))
, _shadowBuffer(PrepareImage(_outer)) {
	_inner.translate(QRect({}, _outer).center() - _inner.center());
}

ImageSubrect RoundAreaWithShadow::validateOverlayMask(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		int twiceRadius,
		float64 scale) {
	const auto ratio = style::DevicePixelRatio();
	const auto cached = (scale == 1.);
	const auto full = cached
		? FrameCacheRect(frameIndex, kOverlayMaskCacheIndex, _overlay)
		: QRect(QPoint(), _overlay * ratio);

	const auto minWidth = twiceRadius + _outer.width() - _inner.width();
	const auto minHeight = twiceRadius + _outer.height() - _inner.height();
	const auto maskSize = QSize(
		std::max(_outer.width(), minWidth),
		std::max(_outer.height(), minHeight));

	const auto result = ImageSubrect{
		cached ? &_overlayCacheParts : &_overlayMaskScaled,
		QRect(full.topLeft(), maskSize * ratio),
	};
	if (cached && _validOverlayMask[frameIndex]) {
		return result;
	}

	auto p = QPainter(result.image.get());
	const auto position = full.topLeft() / ratio;
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(QRect(position, maskSize), Qt::transparent);

	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	auto hq = PainterHighQualityEnabler(p);
	const auto inner = QRect(position + _inner.topLeft(), innerSize);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	if (scale != 1.) {
		const auto center = inner.center();
		p.save();
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.drawRoundedRect(inner, radius, radius);
	if (scale != 1.) {
		p.restore();
	}

	if (cached) {
		_validOverlayMask[frameIndex] = true;
	}
	return result;
}

ImageSubrect RoundAreaWithShadow::validateOverlayShadow(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		int twiceRadius,
		float64 scale,
		const ImageSubrect &mask) {
	const auto ratio = style::DevicePixelRatio();
	const auto cached = (scale == 1.);
	const auto full = cached
		? FrameCacheRect(frameIndex, kOverlayShadowCacheIndex, _overlay)
		: QRect(QPoint(), _overlay * ratio);

	const auto minWidth = twiceRadius + _outer.width() - _inner.width();
	const auto minHeight = twiceRadius + _outer.height() - _inner.height();
	const auto maskSize = QSize(
		std::max(_outer.width(), minWidth),
		std::max(_outer.height(), minHeight));

	const auto result = ImageSubrect{
		cached ? &_overlayCacheParts : &_overlayShadowScaled,
		QRect(full.topLeft(), maskSize * ratio),
	};
	if (cached && _validOverlayShadow[frameIndex]) {
		return result;
	}

	const auto position = full.topLeft() / ratio;

	_overlayShadowScaled.fill(Qt::transparent);
	const auto inner = QRect(_inner.topLeft(), innerSize);
	const auto add = style::ConvertScale(2.5);
	const auto shift = style::ConvertScale(0.5);
	const auto extended = QRectF(inner).marginsAdded({ add, add, add, add });
	{
		auto p = QPainter(&_overlayShadowScaled);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_shadow);
		if (scale != 1.) {
			const auto center = inner.center();
			p.translate(center);
			p.scale(scale, scale);
			p.translate(-center);
		}
		p.drawRoundedRect(extended.translated(0, shift), radius, radius);
		p.end();
	}

	_overlayShadowScaled = Images::Blur(std::move(_overlayShadowScaled));

	auto q = Painter(result.image);
	if (result.image != &_overlayShadowScaled) {
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.drawImage(
			QRect(position, maskSize),
			_overlayShadowScaled,
			QRect(QPoint(), maskSize * ratio));
	}
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.drawImage(QRect(position, maskSize), *mask.image, mask.rect);

	if (cached) {
		_validOverlayShadow[frameIndex] = true;
	}
	return result;
}

void RoundAreaWithShadow::overlayExpandedBorder(
		QPainter &p,
		QSize size,
		float64 expandRatio,
		float64 radiusFrom,
		float64 radiusTill,
		float64 scale) {
	const auto progress = expandRatio;
	const auto frame = int(base::SafeRound(progress * (kFramesCount - 1)));
	const auto cacheRatio = frame / float64(kFramesCount - 1);
	const auto radius = radiusFrom + (radiusTill - radiusFrom) * cacheRatio;
	const auto twiceRadius = int(base::SafeRound(radius * 2));
	const auto innerSize = QSize(
		std::max(_inner.width(), twiceRadius),
		std::max(_inner.height(), twiceRadius));

	const auto overlayMask = validateOverlayMask(
		frame,
		innerSize,
		radius,
		twiceRadius,
		scale);
	const auto overlayShadow = validateOverlayShadow(
		frame,
		innerSize,
		radius,
		twiceRadius,
		scale,
		overlayMask);

	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	FillWithImage(p, QRect(QPoint(), size), overlayMask);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	FillWithImage(p, QRect(QPoint(), size), overlayShadow);
}

QRect RoundAreaWithShadow::FillWithImage(
		QPainter &p,
		QRect geometry,
		const ImageSubrect &pattern) {
	const auto factor = style::DevicePixelRatio();
	const auto &image = *pattern.image;
	const auto source = pattern.rect;
	const auto sourceWidth = (source.width() / factor);
	const auto sourceHeight = (source.height() / factor);
	if (geometry.width() == sourceWidth) {
		const auto part = (sourceHeight / 2) - 1;
		const auto fill = geometry.height() - 2 * part;
		const auto half = part * factor;
		const auto top = source.height() - half;
		p.drawImage(
			geometry.topLeft(),
			image,
			QRect(source.x(), source.y(), source.width(), half));
		if (fill > 0) {
			p.drawImage(
				QRect(
					geometry.topLeft() + QPoint(0, part),
					QSize(sourceWidth, fill)),
				image,
				QRect(
					source.x(),
					source.y() + half,
					source.width(),
					top - half));
		}
		p.drawImage(
			geometry.topLeft() + QPoint(0, part + fill),
			image,
			QRect(source.x(), source.y() + top, source.width(), half));
		return QRect();
	} else if (geometry.height() == sourceHeight) {
		const auto part = (sourceWidth / 2) - 1;
		const auto fill = geometry.width() - 2 * part;
		const auto half = part * factor;
		const auto left = source.width() - half;
		p.drawImage(
			geometry.topLeft(),
			image,
			QRect(source.x(), source.y(), half, source.height()));
		if (fill > 0) {
			p.drawImage(
				QRect(
					geometry.topLeft() + QPoint(part, 0),
					QSize(fill, sourceHeight)),
				image,
				QRect(
					source.x() + half,
					source.y(),
					left - half,
					source.height()));
		}
		p.drawImage(
			geometry.topLeft() + QPoint(part + fill, 0),
			image,
			QRect(source.x() + left, source.y(), half, source.height()));
		return QRect();
	} else if (geometry.width() > sourceWidth
		&& geometry.height() > sourceHeight) {
		const auto xpart = (sourceWidth / 2) - 1;
		const auto xfill = geometry.width() - 2 * xpart;
		const auto xhalf = xpart * factor;
		const auto left = source.width() - xhalf;
		const auto ypart = (sourceHeight / 2) - 1;
		const auto yfill = geometry.height() - 2 * ypart;
		const auto yhalf = ypart * factor;
		const auto top = source.height() - yhalf;
		p.drawImage(
			geometry.topLeft(),
			image,
			QRect(source.x(), source.y(), xhalf, yhalf));
		if (xfill > 0) {
			p.drawImage(
				QRect(
					geometry.topLeft() + QPoint(xpart, 0),
					QSize(xfill, ypart)),
				image,
				QRect(
					source.x() + xhalf,
					source.y(),
					left - xhalf,
					yhalf));
		}
		p.drawImage(
			geometry.topLeft() + QPoint(xpart + xfill, 0),
			image,
			QRect(source.x() + left, source.y(), xhalf, yhalf));

		if (yfill > 0) {
			p.drawImage(
				QRect(
					geometry.topLeft() + QPoint(0, ypart),
					QSize(xpart, yfill)),
				image,
				QRect(
					source.x(),
					source.y() + yhalf,
					xhalf,
					top - yhalf));
			p.drawImage(
				QRect(
					geometry.topLeft() + QPoint(xpart + xfill, ypart),
					QSize(xpart, yfill)),
				image,
				QRect(
					source.x() + left,
					source.y() + yhalf,
					xhalf,
					top - yhalf));
		}

		p.drawImage(
			geometry.topLeft() + QPoint(0, ypart + yfill),
			image,
			QRect(source.x(), source.y() + top, xhalf, yhalf));
		if (xfill > 0) {
			p.drawImage(
				QRect(
					geometry.topLeft() + QPoint(xpart, ypart + yfill),
					QSize(xfill, ypart)),
				image,
				QRect(
					source.x() + xhalf,
					source.y() + top,
					left - xhalf,
					yhalf));
		}
		p.drawImage(
			geometry.topLeft() + QPoint(xpart + xfill, ypart + yfill),
			image,
			QRect(source.x() + left, source.y() + top, xhalf, yhalf));

		return QRect(
			geometry.topLeft() + QPoint(xpart, ypart),
			QSize(xfill, yfill));
	} else {
		Unexpected("Values in RoundAreaWithShadow::fillWithImage.");
	}
}

void RoundAreaWithShadow::setShadowColor(const QColor &shadow) {
	if (_shadow == shadow) {
		return;
	}
	_shadow = shadow;
	ranges::fill(_validBg, false);
	ranges::fill(_validShadow, false);
	ranges::fill(_validOverlayShadow, false);
}

QRect RoundAreaWithShadow::validateShadow(
		int frameIndex,
		float64 scale,
		float64 radius) {
	const auto rect = FrameCacheRect(frameIndex, kShadowCacheIndex, _outer);
	if (_validShadow[frameIndex]) {
		return rect;
	}

	_shadowBuffer.fill(Qt::transparent);
	auto p = QPainter(&_shadowBuffer);
	auto hq = PainterHighQualityEnabler(p);
	const auto center = _inner.center();
	const auto add = style::ConvertScale(2.5);
	const auto shift = style::ConvertScale(0.5);
	const auto big = QRectF(_inner).marginsAdded({ add, add, add, add });
	p.setPen(Qt::NoPen);
	p.setBrush(_shadow);
	if (scale != 1.) {
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.drawRoundedRect(big.translated(0, shift), radius, radius);
	p.end();
	_shadowBuffer = Images::Blur(std::move(_shadowBuffer));

	auto q = QPainter(&_shadowParts);
	q.setCompositionMode(QPainter::CompositionMode_Source);
	q.drawImage(rect.topLeft() / style::DevicePixelRatio(), _shadowBuffer);

	_validShadow[frameIndex] = true;
	return rect;
}

void RoundAreaWithShadow::setBackgroundColor(const QColor &background) {
	if (_background == background) {
		return;
	}
	_background = background;
	ranges::fill(_validBg, false);
}

ImageSubrect RoundAreaWithShadow::validateFrame(
		int frameIndex,
		float64 scale,
		float64 radius) {
	const auto result = ImageSubrect{
		&_cacheBg,
		FrameCacheRect(frameIndex, kBgCacheIndex, _outer)
	};
	if (_validBg[frameIndex]) {
		return result;
	}

	const auto position = result.rect.topLeft() / style::DevicePixelRatio();
	const auto inner = _inner.translated(position);
	const auto shadowSource = validateShadow(frameIndex, scale, radius);

	auto p = QPainter(&_cacheBg);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.drawImage(position, _shadowParts, shadowSource);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_background);
	if (scale != 1.) {
		const auto center = inner.center();
		p.save();
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.drawRoundedRect(inner, radius, radius);
	if (scale != 1.) {
		p.restore();
	}

	_validBg[frameIndex] = true;
	return result;
}

} // namespace Ui
