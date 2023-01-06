// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/shadow.h"

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
		not_null<TWidget*> target,
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

} // namespace Ui
