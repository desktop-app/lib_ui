// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/paint/arcs.h"

#include "ui/painter.h"

namespace Ui::Paint {

ArcsAnimation::ArcsAnimation(
	const style::ArcsAnimation &st,
	int count,
	VerticalDirection direction,
	int centerX,
	int startY)
: _st(st)
, _center(centerX)
, _start(startY)
, _horizontalDirection(HorizontalDirection::None)
, _verticalDirection(direction)
, _startAngle((st.deltaAngle
	+ ((direction == VerticalDirection::Up) ? 90 : 270)) * 16)
, _spanAngle(-st.deltaAngle * 2 * 16) {
	initArcs(count);
}

ArcsAnimation::ArcsAnimation(
	const style::ArcsAnimation &st,
	int count,
	HorizontalDirection direction,
	int startX,
	int centerY)
: _st(st)
, _center(centerY)
, _start(startX)
, _horizontalDirection(direction)
, _verticalDirection(VerticalDirection::None)
, _startAngle((st.deltaAngle
	+ ((direction == HorizontalDirection::Left) ? 180 : 0)) * 16)
, _spanAngle(-st.deltaAngle * 2 * 16) {
	initArcs(count);
}

void ArcsAnimation::initArcs(int count) {
	_arcs.reserve(count);

	for (auto i = 0; i < count; i++) {
		auto arc = Arc{ .rect = computeArcRect(i) };
		_arcs.push_back(std::move(arc));
	}
}

QRectF ArcsAnimation::computeArcRect(int index) const {
	const auto w = _st.startWidth + _st.deltaWidth * index;
	const auto h = _st.startHeight + _st.deltaHeight * index;
	if (_horizontalDirection != HorizontalDirection::None) {
		auto rect = QRectF(0, _center - h / 2.0, w, h);
		if (_horizontalDirection == HorizontalDirection::Right) {
			rect.moveRight(_start + index * _st.space);
		} else {
			rect.moveLeft(_start - index * _st.space);
		}
		return rect;
	} else if (_verticalDirection != VerticalDirection::None) {
		auto rect = QRectF(_center - w / 2.0, 0, w, h);
		if (_verticalDirection == VerticalDirection::Up) {
			rect.moveTop(_start - index * _st.space);
		} else {
			rect.moveBottom(_start + index * _st.space);
		}
		return rect;
	}
	return QRectF();
}

void ArcsAnimation::paint(Painter &p, std::optional<QColor> colorOverride) {
	PainterHighQualityEnabler hq(p);
	QPen pen;
	pen.setWidth(_st.stroke);
	pen.setCapStyle(Qt::RoundCap);
	pen.setColor(colorOverride ? (*colorOverride) : _st.fg->c);
	p.setPen(pen);
	for (const auto &arc : _arcs) {
		p.drawArc(arc.rect, _startAngle, _spanAngle);
	}
}


} // namespace Ui::Paint
