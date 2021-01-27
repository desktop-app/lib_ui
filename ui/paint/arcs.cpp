// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/paint/arcs.h"

#include "ui/effects/animation_value.h"
#include "ui/painter.h"

namespace Ui::Paint {
namespace {

inline float64 InterpolateF(float a, float b, float64 b_ratio) {
	return a + float64(b - a) * b_ratio;
};

QRectF InterpolatedRect(const QRectF &r1, const QRectF &r2, float64 ratio) {
	return QRectF(
		InterpolateF(r1.x(), r2.x(), ratio),
		InterpolateF(r1.y(), r2.y(), ratio),
		InterpolateF(r1.width(), r2.width(), ratio),
		InterpolateF(r1.height(), r2.height(), ratio));
}

} // namespace

ArcsAnimation::ArcsAnimation(
	const style::ArcsAnimation &st,
	std::vector<float> thresholds,
	float64 startValue,
	VerticalDirection direction)
: _st(st)
, _horizontalDirection(HorizontalDirection::None)
, _verticalDirection(direction)
, _startAngle((st.deltaAngle
	+ ((direction == VerticalDirection::Up) ? 90 : 270)) * 16)
, _spanAngle(-st.deltaAngle * 2 * 16)
, _emptyRect(computeArcRect(0))
, _currentValue(startValue) {
	initArcs(std::move(thresholds));
}

ArcsAnimation::ArcsAnimation(
	const style::ArcsAnimation &st,
	std::vector<float> thresholds,
	float64 startValue,
	HorizontalDirection direction)
: _st(st)
, _horizontalDirection(direction)
, _verticalDirection(VerticalDirection::None)
, _startAngle((st.deltaAngle
	+ ((direction == HorizontalDirection::Left) ? 180 : 0)) * 16)
, _spanAngle(-st.deltaAngle * 2 * 16)
, _emptyRect(computeArcRect(0))
, _currentValue(startValue) {
	initArcs(std::move(thresholds));
}

void ArcsAnimation::initArcs(std::vector<float> thresholds) {
	const auto count = thresholds.size();
	_arcs.reserve(count);

	for (auto i = 0; i < count; i++) {
		const auto threshold = thresholds[i];
		const auto progress = (threshold > _currentValue) ? 1. : 0.;
		auto arc = Arc{
			.rect = computeArcRect(i + 1),
			.threshold = threshold,
			.progress = progress,
		};
		_arcs.push_back(std::move(arc));
	}
}

QRectF ArcsAnimation::computeArcRect(int index) const {
	const auto w = _st.startWidth + _st.deltaWidth * index;
	const auto h = _st.startHeight + _st.deltaHeight * index;
	if (_horizontalDirection != HorizontalDirection::None) {
		auto rect = QRectF(0, -h / 2.0, w, h);
		if (_horizontalDirection == HorizontalDirection::Right) {
			rect.moveRight(index * _st.space);
		} else {
			rect.moveLeft(-index * _st.space);
		}
		return rect;
	} else if (_verticalDirection != VerticalDirection::None) {
		auto rect = QRectF(-w / 2.0, 0, w, h);
		if (_verticalDirection == VerticalDirection::Up) {
			rect.moveTop(-index * _st.space);
		} else {
			rect.moveBottom(index * _st.space);
		}
		return rect;
	}
	return QRectF();
}

void ArcsAnimation::update(crl::time now) {
	for (auto &arc : _arcs) {
		if (!isArcFinished(arc)) {
			const auto progress = std::clamp(
				(now - arc.startTime) / float64(_st.duration),
				0.,
				1.);
			arc.progress = (arc.threshold > _currentValue)
				? progress
				: (1. - progress);
		}
	}
	if (isFinished()) {
		_stopUpdateRequests.fire({});
	}
}


void ArcsAnimation::setValue(float64 value) {
	if (_currentValue == value) {
		return;
	}
	const auto previousValue = _currentValue;
	_currentValue = value;
	if (!isFinished()) {
		const auto now = crl::now();
		_startUpdateRequests.fire({});
		for (auto &arc : _arcs) {
			updateArcStartTime(arc, previousValue, now);
		}
	}
}

void ArcsAnimation::updateArcStartTime(
		Arc &arc,
		float64 previousValue,
		crl::time now) {
	if ((arc.progress == 0.) || (arc.progress == 1.)) {
		arc.startTime = isArcFinished(arc) ? 0 : now;
		return;
	}
	const auto isPreviousToHide = (arc.threshold <= previousValue); // 0 -> 1
	const auto isCurrentToHide = (arc.threshold <= _currentValue);
	if (isPreviousToHide != isCurrentToHide) {
		const auto passedTime = _st.duration * arc.progress;
		const auto newDelta = isCurrentToHide
			? (_st.duration - passedTime)
			: passedTime;
		arc.startTime = now - newDelta;
	}
}

float ArcsAnimation::width() const {
	if (_arcs.empty()) {
		return 0;
	}
	for (const auto &arc : ranges::view::reverse(_arcs)) {
		if ((arc.progress != 1.)) {
			return arc.rect.x() + arc.rect.width();
		}
	}
	return 0;
}

float ArcsAnimation::finishedWidth() const {
	if (_arcs.empty()) {
		return 0;
	}
	for (const auto &arc : ranges::view::reverse(_arcs)) {
		if (arc.threshold <= _currentValue) {
			return arc.rect.x() + arc.rect.width();
		}
	}
	return 0;
}

float ArcsAnimation::maxWidth() const {
	if (_arcs.empty()) {
		return 0;
	}
	const auto &r = _arcs.back().rect;
	return r.x() + r.width();
}

float ArcsAnimation::height() const {
	return _arcs.empty()
		? 0
		: _arcs.back().rect.height();
}

rpl::producer<> ArcsAnimation::startUpdateRequests() {
	return _startUpdateRequests.events();
}

rpl::producer<> ArcsAnimation::stopUpdateRequests() {
	return _stopUpdateRequests.events();
}

bool ArcsAnimation::isFinished() const {
	return ranges::all_of(
		_arcs,
		[=](const Arc &arc) { return isArcFinished(arc); });
}

bool ArcsAnimation::isArcFinished(const Arc &arc) const {
	return ((arc.threshold > _currentValue) && (arc.progress == 1.))
		|| ((arc.threshold <= _currentValue) && (arc.progress == 0.));
}

void ArcsAnimation::paint(Painter &p, std::optional<QColor> colorOverride) {
	PainterHighQualityEnabler hq(p);
	QPen pen;
	if (_strokeRatio) {
		pen.setWidthF(_st.stroke * _strokeRatio);
	} else {
		pen.setWidth(_st.stroke);
	}
	pen.setCapStyle(Qt::RoundCap);
	pen.setColor(colorOverride ? (*colorOverride) : _st.fg->c);
	p.setPen(pen);
	for (auto i = 0; i < _arcs.size(); i++) {
		const auto &arc = _arcs[i];
		const auto previousRect = (!i) ? _emptyRect : _arcs[i - 1].rect;
		const auto progress = arc.progress;
		const auto opactity = (1. - progress);
		p.setOpacity(opactity * opactity);
		const auto rect = (progress == 0.)
			? arc.rect
			: (progress == 1.)
			? previousRect
			: InterpolatedRect(arc.rect, previousRect, progress);
		p.drawArc(rect, _startAngle, _spanAngle);
	}
}

void ArcsAnimation::setStrokeRatio(float ratio) {
	_strokeRatio = ratio;
}

} // namespace Ui::Paint
