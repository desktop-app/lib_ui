// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/cross_line.h"

#include "ui/painter.h"

namespace Ui {

CrossLineAnimation::CrossLineAnimation(
	const style::CrossLineAnimation &st,
	bool reversed,
	float angle)
: _st(st)
, _reversed(reversed)
, _transparentPen(Qt::transparent, st.stroke, Qt::SolidLine, Qt::RoundCap)
, _strokePen(st.fg, st.stroke, Qt::SolidLine, Qt::RoundCap)
, _line(st.startPosition, st.endPosition) {
	_line.setAngle(angle);
}

void CrossLineAnimation::paint(
		Painter &p,
		QPoint position,
		float64 progress,
		std::optional<QColor> colorOverride) {
	paint(p, position.x(), position.y(), progress, colorOverride);
}

void CrossLineAnimation::paint(
		Painter &p,
		int left,
		int top,
		float64 progress,
		std::optional<QColor> colorOverride) {
	if (progress == 0.) {
		if (colorOverride) {
			_st.icon.paint(p, left, top, _st.icon.width(), *colorOverride);
		} else {
			_st.icon.paint(p, left, top, _st.icon.width());
		}
	} else if (progress == 1.) {
		auto &complete = colorOverride
			? _completeCrossOverride
			: _completeCross;
		if (complete.isNull()) {
			fillFrame(progress, colorOverride);
			complete = _frame;
		}
		p.drawImage(left, top, complete);
	} else {
		fillFrame(progress, colorOverride);
		p.drawImage(left, top, _frame);
	}
}

void CrossLineAnimation::fillFrame(
		float64 progress,
		std::optional<QColor> colorOverride) {
	const auto ratio = style::DevicePixelRatio();
	if (_frame.isNull()) {
		_frame = QImage(
			_st.icon.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);

	auto topLine = _line;
	topLine.setLength(topLine.length() * progress);
	auto bottomLine = topLine.translated(0, _strokePen.widthF() + 1);

	Painter q(&_frame);
	PainterHighQualityEnabler hq(q);
	if (colorOverride) {
		_st.icon.paint(q, 0, 0, _st.icon.width(), *colorOverride);
	} else {
		_st.icon.paint(q, 0, 0, _st.icon.width());
	}

	if (colorOverride) {
		auto pen = _strokePen;
		pen.setColor(*colorOverride);
		q.setPen(pen);
	} else {
		q.setPen(_strokePen);
	}
	q.drawLine(_reversed ? topLine : bottomLine);

	q.setCompositionMode(QPainter::CompositionMode_Source);
	q.setPen(_transparentPen);
	q.drawLine(_reversed ? bottomLine : topLine);
}

void CrossLineAnimation::invalidate() {
	_completeCross = QImage();
	_completeCrossOverride = QImage();
	_strokePen = QPen(_st.fg, _st.stroke, Qt::SolidLine, Qt::RoundCap);
}

} // namespace Ui
