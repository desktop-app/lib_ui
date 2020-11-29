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
	float angle)
: _st(st)
, _transparentPen(Qt::transparent, st.stroke, Qt::SolidLine, Qt::RoundCap)
, _strokePen(st.fg, st.stroke, Qt::SolidLine, Qt::RoundCap)
, _line(st.startPosition, st.endPosition)
, _completeCross(image(1.)) {
	_line.setAngle(angle);
}

void CrossLineAnimation::paint(
		Painter &p,
		int left,
		int top,
		float64 progress) {
	if (progress == 0.) {
		_st.icon.paint(p, left, top, _st.icon.width());
	} else if (progress == 1.) {
		p.drawImage(left, top, _completeCross);
	} else {
		p.drawImage(left, top, image(progress));
	}
}

QImage CrossLineAnimation::image(float64 progress) const {
	const auto ratio = style::DevicePixelRatio();
	auto frame = QImage(
		QSize(_st.icon.width() * ratio, _st.icon.height() * ratio),
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(ratio);
	frame.fill(Qt::transparent);

	auto topLine = _line;
	topLine.setLength(topLine.length() * progress);
	auto bottomLine = topLine.translated(0, _strokePen.widthF() + 1);

	Painter q(&frame);
	PainterHighQualityEnabler hq(q);
	_st.icon.paint(q, 0, 0, _st.icon.width());

	q.setPen(_strokePen);
	q.drawLine(bottomLine);

	q.setCompositionMode(QPainter::CompositionMode_Source);
	q.setPen(_transparentPen);
	q.drawLine(topLine);

	return frame;
}

} // namespace Ui
