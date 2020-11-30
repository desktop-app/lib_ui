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
, _pen(Qt::NoBrush, 0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) {
	_radius.resize(n);
	_angle.resize(n);
	_radiusNext.resize(n);
	_angleNext.resize(n);
	_progress.resize(n);
	_speed.resize(n);
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
		const auto progress = _progress[i];
		const auto nextIndex = i + 1 < _segmentsCount ? (i + 1) : 0;
		const auto progressNext = _progress[nextIndex];
		const auto r1 = _radius[i] * (1. - progress)
			+ _radiusNext[i] * progress;
		const auto r2 = _radius[nextIndex] * (1. - progressNext)
			+ _radiusNext[nextIndex] * progressNext;
		const auto angle1 = _angle[i] * (1. - progress)
			+ _angleNext[i] * progress;
		const auto angle2 = _angle[nextIndex] * (1. - progressNext)
			+ _angleNext[nextIndex] * progressNext;

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
		generateBlob(_radius, _angle, i);
		generateBlob(_radiusNext, _angleNext, i);
		_progress[i] = 0;
	}
}

void BlobBezier::generateBlob(
		std::vector<float> &radius,
		std::vector<float> &angle,
		int i) {
	const auto angleSegment = 360. / _segmentsCount;
	const auto angleDiff = angleSegment * 0.05;
	const auto radDiff = _maxRadius - _minRadius;

	radius[i] = _minRadius + std::abs(RandomAdditional()) * radDiff;
	angle[i] = angleSegment * i + RandomAdditional() * angleDiff;
	_speed[i] = 0.017 + 0.003 * std::abs(RandomAdditional());
}

void BlobBezier::update(float level, float speedScale) {
	_scale = level;
	for (auto i = 0; i < _segmentsCount; i++) {
		_progress[i] += (_speed[i] * _minSpeed)
			+ level * _speed[i] * _maxSpeed * speedScale;
		if (_progress[i] >= 1) {
			_progress[i] = 0.;
			_radius[i] = _radiusNext[i];
			_angle[i] = _angleNext[i];
			generateBlob(_radiusNext, _angleNext, i);
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
