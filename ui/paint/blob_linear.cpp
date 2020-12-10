// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/paint/blob_linear.h"

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

LinearBlobBezier::LinearBlobBezier(
	int n,
	float minScale,
	float minSpeed,
	float maxSpeed)
: _segmentsCount(n)
, _minSpeed(minSpeed ? minSpeed : kMinSpeed)
, _maxSpeed(maxSpeed ? maxSpeed : kMaxSpeed)
, _pen(Qt::NoBrush, 0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
, _segments(n + 1) {
}

void LinearBlobBezier::paint(
		Painter &p,
		const QBrush &brush,
		const QRect &rect,
		float pinnedTop,
		float progressToPinned) {
	auto path = QPainterPath();
	auto m = QTransform();

	const auto left = rect.x();
	const auto top = rect.y();
	const auto right = rect.x() + rect.width();
	const auto bottom = rect.y() + rect.height();

	// path.moveTo(right, bottom);
	// path.lineTo(left, bottom);
	path.moveTo(right, top);
	path.lineTo(left, top);

	const auto &n = _segmentsCount;

	p.save();

	for (auto i = 0; i <= n; i++) {
		const auto &segment = _segments[i];

		if (!i) {
			const auto progress = segment.progress;
			const auto r1 = segment.radius * (1. - progress)
				+ segment.radiusNext * progress;
			const auto y = (bottom + r1);// * progressToPinned
			// const auto y = (top - r1) * progressToPinned
				// + pinnedTop * (1. - progressToPinned);
			path.lineTo(left, y);
		} else {
			const auto &prevSegment = _segments[i - 1];
			const auto progress = prevSegment.progress;
			const auto r1 = prevSegment.radius * (1. - progress)
				+ prevSegment.radiusNext * progress;

			const auto progressNext = segment.progress;
			const auto r2 = segment.radius * (1. - progressNext)
				+ segment.radiusNext * progressNext;

			const auto x1 = (right - left) / n * (i - 1);
			const auto x2 = (right - left) / n * i;
			const auto cx = x1 + (x2 - x1) / 2;

			const auto y1 = (bottom + r1);// * progressToPinned
			// const auto y1 = (top - r1) * progressToPinned
				// + pinnedTop * (1. - progressToPinned);
			const auto y2 = (bottom + r2);// * progressToPinned
			// const auto y2 = (top - r2) * progressToPinned
				// + pinnedTop * (1. - progressToPinned);
			path.cubicTo(
				QPointF(cx, y1),
				QPointF(cx, y2),
				QPointF(x2, y2)
			);
			if (i == n) {
				path.lineTo(right, top);
				// path.lineTo(right, bottom);
			}
		}
	}

	p.setBrush(Qt::NoBrush);

	p.setPen(_pen);
	p.fillPath(path, brush);
	p.drawPath(path);

	p.restore();
}

void LinearBlobBezier::generateBlob() {
	for (auto i = 0; i < _segmentsCount; i++) {
		auto &segment = _segments[i];
		generateBlob(segment.radius, i);
		generateBlob(segment.radiusNext, i);
		segment.progress = 0.;
	}
}

void LinearBlobBezier::generateBlob(float &radius, int i) {
	const auto radDiff = _radiuses.max - _radiuses.min;

	radius = _radiuses.min + std::abs(RandomAdditional()) * radDiff;
	_segments[i].speed = 0.017 + 0.003 * std::abs(RandomAdditional());
}

void LinearBlobBezier::update(float level, float speedScale) {
	_scale = level;
	for (auto i = 0; i < _segmentsCount; i++) {
		auto &segment = _segments[i];
		segment.progress += (segment.speed * _minSpeed)
			+ level * segment.speed * _maxSpeed * speedScale;
		if (segment.progress >= 1) {
			segment.progress = 0.;
			segment.radius = segment.radiusNext;
			generateBlob(segment.radiusNext, i);
		}
	}
}

void LinearBlobBezier::setRadiuses(Radiuses values) {
	_radiuses = values;
}

LinearBlobBezier::Radiuses LinearBlobBezier::radiuses() const {
	return _radiuses;
}

} // namespace Ui::Paint
