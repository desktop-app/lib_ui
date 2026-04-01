// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/shadow.h"

#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>

namespace Ui {
namespace {

struct CustomImage {
public:
	explicit CustomImage(const QImage &image)
	: _image(image) {
	}
	void paint(QPainter &p, int x, int y, int outerw) const {
		p.drawImage(x, y, _image);
	}
	void fill(QPainter &p, QRect rect) const {
		p.drawImage(rect, _image);
	}
	[[nodiscard]] bool empty() const {
		return _image.isNull();
	}
	[[nodiscard]] int width() const {
		return _image.width() / style::DevicePixelRatio();
	}
	[[nodiscard]] int height() const {
		return _image.height() / style::DevicePixelRatio();
	}

private:
	const QImage &_image;

};

struct CustomShadowCorners {
	const style::icon &left;
	CustomImage topLeft;
	const style::icon &top;
	CustomImage topRight;
	const style::icon &right;
	CustomImage bottomRight;
	const style::icon &bottom;
	CustomImage bottomLeft;
	const style::margins &extend;
};

struct CustomShadow {
	CustomImage left;
	CustomImage topLeft;
	CustomImage top;
	CustomImage topRight;
	CustomImage right;
	CustomImage bottomRight;
	CustomImage bottom;
	CustomImage bottomLeft;
	const style::margins &extend;
};

template <typename Shadow>
void ShadowPaint(QPainter &p, const QRect &box, int outerWidth, const Shadow &st, RectParts sides) {
	auto left = (sides & RectPart::Left);
	auto top = (sides & RectPart::Top);
	auto right = (sides & RectPart::Right);
	auto bottom = (sides & RectPart::Bottom);
	if (left) {
		auto from = box.y();
		auto to = from + box.height();
		if (top && !st.topLeft.empty()) {
			st.topLeft.paint(p, box.x() - st.extend.left(), box.y() - st.extend.top(), outerWidth);
			from += st.topLeft.height() - st.extend.top();
		}
		if (bottom && !st.bottomLeft.empty()) {
			st.bottomLeft.paint(p, box.x() - st.extend.left(), box.y() + box.height() + st.extend.bottom() - st.bottomLeft.height(), outerWidth);
			to -= st.bottomLeft.height() - st.extend.bottom();
		}
		if (to > from && !st.left.empty()) {
			st.left.fill(p, style::rtlrect(box.x() - st.extend.left(), from, st.left.width(), to - from, outerWidth));
		}
	}
	if (right) {
		auto from = box.y();
		auto to = from + box.height();
		if (top && !st.topRight.empty()) {
			st.topRight.paint(p, box.x() + box.width() + st.extend.right() - st.topRight.width(), box.y() - st.extend.top(), outerWidth);
			from += st.topRight.height() - st.extend.top();
		}
		if (bottom && !st.bottomRight.empty()) {
			st.bottomRight.paint(p, box.x() + box.width() + st.extend.right() - st.bottomRight.width(), box.y() + box.height() + st.extend.bottom() - st.bottomRight.height(), outerWidth);
			to -= st.bottomRight.height() - st.extend.bottom();
		}
		if (to > from && !st.right.empty()) {
			st.right.fill(p, style::rtlrect(box.x() + box.width() + st.extend.right() - st.right.width(), from, st.right.width(), to - from, outerWidth));
		}
	}
	if (top && !st.top.empty()) {
		auto from = box.x();
		auto to = from + box.width();
		if (left && !st.topLeft.empty()) from += st.topLeft.width() - st.extend.left();
		if (right && !st.topRight.empty()) to -= st.topRight.width() - st.extend.right();
		if (to > from) {
			st.top.fill(p, style::rtlrect(from, box.y() - st.extend.top(), to - from, st.top.height(), outerWidth));
		}
	}
	if (bottom && !st.bottom.empty()) {
		auto from = box.x();
		auto to = from + box.width();
		if (left && !st.bottomLeft.empty()) from += st.bottomLeft.width() - st.extend.left();
		if (right && !st.bottomRight.empty()) to -= st.bottomRight.width() - st.extend.right();
		if (to > from) {
			st.bottom.fill(p, style::rtlrect(from, box.y() + box.height() + st.extend.bottom() - st.bottom.height(), to - from, st.bottom.height(), outerWidth));
		}
	}
}

} // namespace

PlainShadow::PlainShadow(QWidget *parent)
: PlainShadow(parent, st::shadowFg) {
}

PlainShadow::PlainShadow(QWidget *parent, style::color color)
: RpWidget(parent)
, _color(color) {
	resize(st::lineWidth, st::lineWidth);
}

void PlainShadow::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(e->rect(), _color);
}

void Shadow::paint(QPainter &p, const QRect &box, int outerWidth, const style::Shadow &st, RectParts sides) {
	ShadowPaint<style::Shadow>(p, box, outerWidth, st, sides);
}

void Shadow::paint(
		QPainter &p,
		const QRect &box,
		int outerWidth,
		const style::Shadow &st,
		const std::array<QImage, 4> &corners,
		RectParts sides) {
	const auto shadow = CustomShadowCorners{
		.left = st.left,
		.topLeft = CustomImage(corners[0]),
		.top = st.top,
		.topRight = CustomImage(corners[2]),
		.right = st.right,
		.bottomRight = CustomImage(corners[3]),
		.bottom = st.bottom,
		.bottomLeft = CustomImage(corners[1]),
		.extend = st.extend,
	};
	ShadowPaint<CustomShadowCorners>(p, box, outerWidth, shadow, sides);
}

void Shadow::paint(
		QPainter &p,
		const QRect &box,
		int outerWidth,
		const style::Shadow &st,
		const std::array<QImage, 4> &sides,
		const std::array<QImage, 4> &corners) {
	const auto shadow = CustomShadow{
		.left = CustomImage(sides[0]),
		.topLeft = CustomImage(corners[0]),
		.top = CustomImage(sides[1]),
		.topRight = CustomImage(corners[2]),
		.right = CustomImage(sides[2]),
		.bottomRight = CustomImage(corners[3]),
		.bottom = CustomImage(sides[3]),
		.bottomLeft = CustomImage(corners[1]),
		.extend = st.extend,
	};
	ShadowPaint<CustomShadow>(p, box, outerWidth, shadow, RectPart()
		| (sides[0].isNull() ? RectPart() : RectPart::Left)
		| (sides[1].isNull() ? RectPart() : RectPart::Top)
		| (sides[2].isNull() ? RectPart() : RectPart::Right)
		| (sides[3].isNull() ? RectPart() : RectPart::Bottom));
}

QPixmap Shadow::grab(
		not_null<RpWidget*> target,
		const style::Shadow &shadow,
		RectParts sides) {
	SendPendingMoveResizeEvents(target);
	auto rect = target->rect();
	auto extend = QMargins(
		(sides & RectPart::Left) ? shadow.extend.left() : 0,
		(sides & RectPart::Top) ? shadow.extend.top() : 0,
		(sides & RectPart::Right) ? shadow.extend.right() : 0,
		(sides & RectPart::Bottom) ? shadow.extend.bottom() : 0
	);
	auto full = QRect(0, 0, extend.left() + rect.width() + extend.right(), extend.top() + rect.height() + extend.bottom());
	auto result = QPixmap(full.size() * style::DevicePixelRatio());
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		QPainter p(&result);
		Shadow::paint(p, full.marginsRemoved(extend), full.width(), shadow);
		RenderWidget(p, target, QPoint(extend.left(), extend.top()));
	}
	return result;
}

void Shadow::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	paint(p, rect().marginsRemoved(_st.extend), width(), _st, _sides);
}

BoxShadow::BoxShadow(const style::BoxShadow &st)
: _blurRadius(st.blurRadius)
, _offset(st.offset)
, _opacity(st.opacity) {
}

QMargins BoxShadow::ExtendFor(const style::BoxShadow &st) {
	const auto blur = st.blurRadius;
	const auto ox = st.offset.x();
	const auto oy = st.offset.y();
	return {
		std::max(blur - ox, 0),
		std::max(blur - oy, 0),
		std::max(blur + ox, 0),
		std::max(blur + oy, 0),
	};
}

float64 BoxShadow::opacity() const {
	return _opacity;
}

QMargins BoxShadow::extend() const {
	const auto ox = _offset.x();
	const auto oy = _offset.y();
	return {
		std::max(_blurRadius - ox, 0),
		std::max(_blurRadius - oy, 0),
		std::max(_blurRadius + ox, 0),
		std::max(_blurRadius + oy, 0),
	};
}

BoxShadow::Grid BoxShadow::preparedGrid(int cornerRadius) const {
	prepare(cornerRadius);
	return { _cache, _cornerL, _cornerT, _cornerR, _cornerB, _middle };
}

void BoxShadow::prepare(int cornerRadius) const {
	if (_cornerRadius == cornerRadius) {
		return;
	}
	_cornerRadius = cornerRadius;
	_middle = 2;

	const auto ext = extend();
	_cornerL = ext.left() + cornerRadius;
	_cornerT = ext.top() + cornerRadius;
	_cornerR = ext.right() + cornerRadius;
	_cornerB = ext.bottom() + cornerRadius;

	const auto bodyW = 2 * cornerRadius + _middle;
	const auto bodyH = bodyW;
	const auto cacheW = _cornerL + _middle + _cornerR;
	const auto cacheH = _cornerT + _middle + _cornerB;

	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		QSize(cacheW, cacheH) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);

	// Shape (shadow source) is always at (blurRadius, blurRadius).
	const auto shape = QRectF(
		_blurRadius,
		_blurRadius,
		bodyW,
		bodyH);
	// Body cutout is shifted by -offset relative to the shape.
	const auto cutout = QRectF(
		ext.left(),
		ext.top(),
		bodyW,
		bodyH);

	const auto drawRounded = [&](QPainter &p, QRectF rect) {
		if (cornerRadius > 0) {
			p.drawRoundedRect(rect, cornerRadius, cornerRadius);
		} else {
			p.drawRect(rect);
		}
	};

	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		drawRounded(p, shape);
	}

	image = Images::BlurLargeImage(
		std::move(image),
		_blurRadius * ratio);

	// Convert blurred RGB mask to black shadow with alpha.
	for (auto y = 0; y < image.height(); ++y) {
		auto row = reinterpret_cast<uint32*>(image.scanLine(y));
		for (auto x = 0; x < image.width(); ++x) {
			const auto mask = row[x] & 0xFFU;
			row[x] = mask << 24;
		}
	}

	// Cut out the body area so the shadow is only the outer glow.
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		drawRounded(p, cutout);
	}

	_cache = std::move(image);
}

void BoxShadow::paint(
		QPainter &p,
		const QRect &box,
		int cornerRadius) const {
	paint(p, box, cornerRadius, _opacity);
}

void BoxShadow::paint(
		QPainter &p,
		const QRect &box,
		int cornerRadius,
		float64 opacity) const {
	prepare(cornerRadius);
	if (_cache.isNull()) {
		return;
	}

	const auto ratio = style::DevicePixelRatio();
	const auto clL = _cornerL;
	const auto clT = _cornerT;
	const auto clR = _cornerR;
	const auto clB = _cornerB;
	const auto clLPx = clL * ratio;
	const auto clTPx = clT * ratio;
	const auto clRPx = clR * ratio;
	const auto clBPx = clB * ratio;
	const auto middlePx = _middle * ratio;

	const auto wasOpacity = p.opacity();
	if (opacity < 1.) {
		p.setOpacity(wasOpacity * opacity);
	}

	const auto ext = extend();
	const auto outer = box.marginsAdded(ext);
	const auto cw = std::max(outer.width() - clL - clR, 0);
	const auto ch = std::max(outer.height() - clT - clB, 0);

	const auto fill = [&](QRect dst, QRect src) {
		if (dst.width() > 0 && dst.height() > 0) {
			p.drawImage(dst, _cache, src);
		}
	};

	// Source positions in physical pixels.
	const auto srcR = clLPx + middlePx;
	const auto srcB = clTPx + middlePx;

	// Corners.
	fill(
		{ outer.x(), outer.y(), clL, clT },
		{ 0, 0, clLPx, clTPx });
	fill(
		{ outer.x() + clL + cw, outer.y(), clR, clT },
		{ srcR, 0, clRPx, clTPx });
	fill(
		{ outer.x(), outer.y() + clT + ch, clL, clB },
		{ 0, srcB, clLPx, clBPx });
	fill(
		{ outer.x() + clL + cw, outer.y() + clT + ch, clR, clB },
		{ srcR, srcB, clRPx, clBPx });

	// Sides (stretched from the uniform middle strip).
	fill(
		{ outer.x() + clL, outer.y(), cw, clT },
		{ clLPx, 0, middlePx, clTPx });
	fill(
		{ outer.x() + clL, outer.y() + clT + ch, cw, clB },
		{ clLPx, srcB, middlePx, clBPx });
	fill(
		{ outer.x(), outer.y() + clT, clL, ch },
		{ 0, clTPx, clLPx, middlePx });
	fill(
		{ outer.x() + clL + cw, outer.y() + clT, clR, ch },
		{ srcR, clTPx, clRPx, middlePx });

	if (opacity < 1.) {
		p.setOpacity(wasOpacity);
	}
}

} // namespace Ui
