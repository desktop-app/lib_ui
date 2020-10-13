// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rect_part.h"
#include "ui/style/style_core.h"

enum class ImageRoundRadius;
class QPainter;

namespace Ui {

void DrawRoundedRect(
	QPainter &p,
	const QRect &rect,
	const QBrush &brush,
	const std::array<QImage, 4> &corners,
	RectParts parts = RectPart::Full);

class RoundRect final {
public:
	RoundRect(ImageRoundRadius radius, const style::color &color);
	RoundRect(int radius, const style::color &color);

	[[nodiscard]] const style::color &color() const;
	void paint(
		QPainter &p,
		const QRect &rect,
		RectParts parts = RectPart::Full) const;
	void paintSomeRounded(
		QPainter &p,
		const QRect &rect,
		RectParts corners) const;

private:
	style::color _color;
	std::array<QImage, 4> _corners;

	rpl::lifetime _lifetime;

};

} // namespace Ui
