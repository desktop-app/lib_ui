// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "styles/style_widgets.h"

class Painter;

namespace Ui::Paint {

class ArcsAnimation {
public:

	enum class HorizontalDirection {
		Left,
		Right,
		None,
	};

	enum class VerticalDirection {
		Up,
		Down,
		None,
	};

	ArcsAnimation(
		const style::ArcsAnimation &st,
		int count,
		VerticalDirection direction,
		int centerX,
		int startY);

	ArcsAnimation(
		const style::ArcsAnimation &st,
		int count,
		HorizontalDirection direction,
		int startX,
		int centerY);

	void paint(
		Painter &p,
		std::optional<QColor> colorOverride = std::nullopt);

private:
	struct Arc {
		QRectF rect;
	};

	void initArcs(int count);
	QRectF computeArcRect(int index) const;

	const style::ArcsAnimation &_st;
	const int _center;
	const int _start;
	const HorizontalDirection _horizontalDirection;
	const VerticalDirection _verticalDirection;
	const int _startAngle;
	const int _spanAngle;

	std::vector<Arc> _arcs;

};

} // namespace Ui::Paint
