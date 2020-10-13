// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/round_rect.h"

#include "ui/style/style_core.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"

namespace Ui {

void DrawRoundedRect(
		QPainter &p,
		const QRect &rect,
		const QBrush &brush,
		const std::array<QImage, 4> &corners,
		RectParts parts) {
	const auto pixelRatio = style::DevicePixelRatio();
	const auto x = rect.x();
	const auto y = rect.y();
	const auto w = rect.width();
	const auto h = rect.height();
	const auto cornerWidth = corners[0].width() / pixelRatio;
	const auto cornerHeight = corners[0].height() / pixelRatio;
	if (w < 2 * cornerWidth || h < 2 * cornerHeight) return;
	if (w > 2 * cornerWidth) {
		if (parts & RectPart::Top) {
			p.fillRect(x + cornerWidth, y, w - 2 * cornerWidth, cornerHeight, brush);
		}
		if (parts & RectPart::Bottom) {
			p.fillRect(x + cornerWidth, y + h - cornerHeight, w - 2 * cornerWidth, cornerHeight, brush);
		}
	}
	if (h > 2 * cornerHeight) {
		if ((parts & RectPart::NoTopBottom) == RectPart::NoTopBottom) {
			p.fillRect(x, y + cornerHeight, w, h - 2 * cornerHeight, brush);
		} else {
			if (parts & RectPart::Left) {
				p.fillRect(x, y + cornerHeight, cornerWidth, h - 2 * cornerHeight, brush);
			}
			if ((parts & RectPart::Center) && w > 2 * cornerWidth) {
				p.fillRect(x + cornerWidth, y + cornerHeight, w - 2 * cornerWidth, h - 2 * cornerHeight, brush);
			}
			if (parts & RectPart::Right) {
				p.fillRect(x + w - cornerWidth, y + cornerHeight, cornerWidth, h - 2 * cornerHeight, brush);
			}
		}
	}
	if (parts & RectPart::TopLeft) {
		p.drawImage(x, y, corners[0]);
	}
	if (parts & RectPart::TopRight) {
		p.drawImage(x + w - cornerWidth, y, corners[1]);
	}
	if (parts & RectPart::BottomLeft) {
		p.drawImage(x, y + h - cornerHeight, corners[2]);
	}
	if (parts & RectPart::BottomRight) {
		p.drawImage(x + w - cornerWidth, y + h - cornerHeight, corners[3]);
	}
}

RoundRect::RoundRect(
	ImageRoundRadius radius,
	const style::color &color)
: _color(color)
, _corners(Images::PrepareCorners(radius, color)) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_corners = Images::PrepareCorners(radius, _color);
	}, _lifetime);
}

RoundRect::RoundRect(
	int radius,
	const style::color &color)
: _color(color)
, _corners(Images::PrepareCorners(radius, color)) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_corners = Images::PrepareCorners(radius, _color);
	}, _lifetime);
}

const style::color &RoundRect::color() const {
	return _color;
}

void RoundRect::paint(
		QPainter &p,
		const QRect &rect,
		RectParts parts) const {
	DrawRoundedRect(p, rect, _color, _corners, parts);
}

void RoundRect::paintSomeRounded(
		QPainter &p,
		const QRect &rect,
		RectParts corners) const {
	DrawRoundedRect(
		p,
		rect,
		_color,
		_corners,
		corners | RectPart::Top | RectPart::NoTopBottom | RectPart::Bottom);

	const auto pixelRatio = style::DevicePixelRatio();
	const auto cornerWidth = _corners[0].width() / pixelRatio;
	const auto cornerHeight = _corners[0].height() / pixelRatio;
	if (!(corners & RectPart::TopLeft)) {
		p.fillRect(rect.x(), rect.y(), cornerWidth, cornerHeight, _color);
	}
	if (!(corners & RectPart::TopRight)) {
		p.fillRect(
			rect.x() + rect.width() - cornerWidth,
			rect.y(),
			cornerWidth,
			cornerHeight,
			_color);
	}
	if (!(corners & RectPart::BottomRight)) {
		p.fillRect(
			rect.x() + rect.width() - cornerWidth,
			rect.y() + rect.height() - cornerHeight,
			cornerWidth,
			cornerHeight,
			_color);
	}
	if (!(corners & RectPart::BottomLeft)) {
		p.fillRect(
			rect.x(),
			rect.y() + rect.height() - cornerHeight,
			cornerWidth,
			cornerHeight,
			_color);
	}
}

} // namespace Ui
