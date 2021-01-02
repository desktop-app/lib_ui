// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/paint/blob.h"

#include "base/openssl_help.h"
#include "ui/painter.h"

#include <QtGui/QPainterPath>
#include <QtCore/QtMath>

namespace Ui::Paint {

namespace {

constexpr auto kMaxSpeed = 8.2;
constexpr auto kMinSpeed = 0.8;

constexpr auto kMinSegmentSpeed = 0.017;
constexpr auto kSegmentSpeedDiff = 0.003;

float64 RandomAdditional() {
	return (openssl::RandomValue<int>() % 100 / 100.);
}

} // namespace

Blob::Blob(int n, float minSpeed, float maxSpeed)
: _segmentsCount(n)
, _minSpeed(minSpeed ? minSpeed : kMinSpeed)
, _maxSpeed(maxSpeed ? maxSpeed : kMaxSpeed)
, _pen(Qt::NoBrush, 0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) {
}

void Blob::generateBlob() {
	for (auto i = 0; i < _segmentsCount; i++) {
		generateSingleValues(i);
		// Fill nexts.
		generateTwoValues(i);
		// Fill currents.
		generateTwoValues(i);
	}
}

void Blob::generateSingleValues(int i) {
	auto &segment = segmentAt(i);
	segment.progress = 0.;
	segment.speed = kMinSegmentSpeed
		+ kSegmentSpeedDiff * std::abs(RandomAdditional());
}

void Blob::update(float level, float speedScale, float64 rate) {
	for (auto i = 0; i < _segmentsCount; i++) {
		auto &segment = segmentAt(i);
		segment.progress += (_minSpeed + level * _maxSpeed * speedScale)
			* segment.speed
			* rate;
		if (segment.progress >= 1) {
			generateSingleValues(i);
			generateTwoValues(i);
		}
	}
}

void Blob::setRadiuses(Radiuses values) {
	_radiuses = values;
}

Blob::Radiuses Blob::radiuses() const {
	return _radiuses;
}

RadialBlob::RadialBlob(int n, float minScale, float minSpeed, float maxSpeed)
: Blob(n, minSpeed, maxSpeed)
, _segmentLength((4.0 / 3.0) * std::tan(M_PI / (2 * n)))
, _minScale(minScale)
, _segmentAngle(360. / n)
, _angleDiff(_segmentAngle * 0.05)
, _segments(n) {
}

void RadialBlob::paint(Painter &p, const QBrush &brush, float outerScale) {
	auto path = QPainterPath();
	auto m = QMatrix();

	p.save();
	const auto scale = (_minScale + (1. - _minScale) * _scale) * outerScale;
	if (scale == 0.) {
		p.restore();
		return;
	} else if (scale != 1.) {
		p.scale(scale, scale);
	}

	for (auto i = 0; i < _segmentsCount; i++) {
		const auto &segment = _segments[i];

		const auto nextIndex = i + 1 < _segmentsCount ? (i + 1) : 0;
		const auto nextSegment = _segments[nextIndex];

		const auto progress = segment.progress;
		const auto progressNext = nextSegment.progress;

		const auto r1 = segment.radius.current * (1. - progress)
			+ segment.radius.next * progress;
		const auto r2 = nextSegment.radius.current * (1. - progressNext)
			+ nextSegment.radius.next * progressNext;
		const auto angle1 = segment.angle.current * (1. - progress)
			+ segment.angle.next * progress;
		const auto angle2 = nextSegment.angle.current * (1. - progressNext)
			+ nextSegment.angle.next * progressNext;

		const auto l = _segmentLength * (std::min(r1, r2)
			+ (std::max(r1, r2) - std::min(r1, r2)) / 2.);

		m.reset();
		m.rotate(angle1);

		const auto pointStart1 = m.map(QPointF(0, -r1));
		const auto pointStart2 = m.map(QPointF(l, -r1));

		m.reset();
		m.rotate(angle2);
		const auto pointEnd1 = m.map(QPointF(0, -r2));
		const auto pointEnd2 = m.map(QPointF(-l, -r2));

		if (i == 0) {
			path.moveTo(pointStart1);
		}

		path.cubicTo(pointStart2, pointEnd2, pointEnd1);
	}

	p.setBrush(Qt::NoBrush);

	p.setPen(_pen);
	p.fillPath(path, brush);
	p.drawPath(path);

	p.restore();
}

void RadialBlob::generateTwoValues(int i) {
	auto &radius = _segments[i].radius;
	auto &angle = _segments[i].angle;

	const auto radDiff = _radiuses.max - _radiuses.min;

	angle.setNext(_segmentAngle * i + RandomAdditional() * _angleDiff);
	radius.setNext(_radiuses.min + std::abs(RandomAdditional()) * radDiff);
}

void RadialBlob::update(float level, float speedScale, float64 rate) {
	_scale = level;
	Blob::update(level, speedScale, rate);
}

Blob::Segment &RadialBlob::segmentAt(int i) {
	return _segments[i];
};

LinearBlob::LinearBlob(
	int n,
	Direction direction,
	float minSpeed,
	float maxSpeed)
: Blob(n + 1)
, _topDown(direction == Direction::TopDown ? 1 : -1)
, _segments(_segmentsCount) {
}

void LinearBlob::paint(Painter &p, const QBrush &brush, int width) {
	if (!width) {
		return;
	}

	auto path = QPainterPath();

	const auto left = 0;
	const auto right = width;

	path.moveTo(right, 0);
	path.lineTo(left, 0);

	const auto n = float(_segmentsCount - 1);

	p.save();

	for (auto i = 0; i < _segmentsCount; i++) {
		const auto &segment = _segments[i];

		if (!i) {
			const auto &progress = segment.progress;
			const auto r1 = segment.radius.current * (1. - progress)
				+ segment.radius.next * progress;
			const auto y = r1 * _topDown;
			path.lineTo(left, y);
		} else {
			const auto &prevSegment = _segments[i - 1];
			const auto &progress = prevSegment.progress;
			const auto r1 = prevSegment.radius.current * (1. - progress)
				+ prevSegment.radius.next * progress;

			const auto &progressNext = segment.progress;
			const auto r2 = segment.radius.current * (1. - progressNext)
				+ segment.radius.next * progressNext;

			const auto x1 = (right - left) / n * (i - 1);
			const auto x2 = (right - left) / n * i;
			const auto cx = x1 + (x2 - x1) / 2;

			const auto y1 = r1 * _topDown;
			const auto y2 = r2 * _topDown;
			path.cubicTo(
				QPointF(cx, y1),
				QPointF(cx, y2),
				QPointF(x2, y2)
			);
		}
	}
	path.lineTo(right, 0);

	p.setBrush(Qt::NoBrush);
	p.setPen(_pen);
	p.fillPath(path, brush);
	p.drawPath(path);

	p.restore();
}

void LinearBlob::generateTwoValues(int i) {
	auto &radius = _segments[i].radius;
	const auto radDiff = _radiuses.max - _radiuses.min;
	radius.setNext(_radiuses.min + std::abs(RandomAdditional()) * radDiff);
}

Blob::Segment &LinearBlob::segmentAt(int i) {
	return _segments[i];
};

} // namespace Ui::Paint
