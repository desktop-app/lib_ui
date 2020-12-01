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

float64 RandomAdditional() {
	return (openssl::RandomValue<int>() % 100 / 100.);
}

} // namespace

BlobBezier::BlobBezier(int n, float minScale, float minSpeed, float maxSpeed)
: _segmentsCount(n)
, _segmentLength((4.0 / 3.0) * std::tan(M_PI / (2 * n)))
, _minScale(minScale)
, _minSpeed(minSpeed ? minSpeed : kMinSpeed)
, _maxSpeed(maxSpeed ? maxSpeed : kMaxSpeed)
, _pen(Qt::NoBrush, 0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
, _segmentAngle(360. / n)
, _segments(n) {
}

void BlobBezier::paint(Painter &p, const QBrush &brush) {
	auto path = QPainterPath();
	auto m = QMatrix();

	p.save();
	const auto scale = _minScale + _scale * (1. - _minScale);
	if (scale != 1.) {
		p.scale(scale, scale);
	}

	for (auto i = 0; i < _segmentsCount; i++) {
		const auto &segment = _segments[i];

		const auto nextIndex = i + 1 < _segmentsCount ? (i + 1) : 0;
		const auto nextSegment = _segments[nextIndex];

		const auto progress = segment.progress;
		const auto progressNext = nextSegment.progress;

		const auto r1 = segment.radius * (1. - progress)
			+ segment.radiusNext * progress;
		const auto r2 = nextSegment.radius * (1. - progressNext)
			+ nextSegment.radiusNext * progressNext;
		const auto angle1 = segment.angle * (1. - progress)
			+ segment.angleNext * progress;
		const auto angle2 = nextSegment.angle * (1. - progressNext)
			+ nextSegment.angleNext * progressNext;

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

void BlobBezier::generateBlob() {
	for (auto i = 0; i < _segmentsCount; i++) {
		auto &segment = _segments[i];
		generateBlob(segment.radius, segment.angle, i);
		generateBlob(segment.radiusNext, segment.angleNext, i);
		segment.progress = 0.;
	}
}

void BlobBezier::generateBlob(float &radius, float &angle, int i) {
	const auto angleDiff = _segmentAngle * 0.05;
	const auto radDiff = _maxRadius - _minRadius;

	radius = _minRadius + std::abs(RandomAdditional()) * radDiff;
	angle = _segmentAngle * i + RandomAdditional() * angleDiff;
	_segments[i].speed = 0.017 + 0.003 * std::abs(RandomAdditional());
}

void BlobBezier::update(float level, float speedScale) {
	_scale = level;
	for (auto i = 0; i < _segmentsCount; i++) {
		auto &segment = _segments[i];
		segment.progress += (segment.speed * _minSpeed)
			+ level * segment.speed * _maxSpeed * speedScale;
		if (segment.progress >= 1) {
			segment.progress = 0.;
			segment.radius = segment.radiusNext;
			segment.angle = segment.angleNext;
			generateBlob(segment.radiusNext, segment.angleNext, i);
		}
	}
}

void BlobBezier::setMinRadius(float value) {
	_minRadius = value;
}

void BlobBezier::setMaxRadius(float value) {
	_maxRadius = value;
}

} // namespace Ui::Paint
