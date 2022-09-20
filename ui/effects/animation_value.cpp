// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/animation_value.h"

#include "ui/painter.h"

#include <QtCore/QtMath> // M_PI

namespace anim {
namespace {

rpl::variable<bool> AnimationsDisabled = false;
int SlowMultiplierMinusOne/* = 0*/;

} // namespace

transition linear = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto result = delta * dt;

	Ensures(!std::isnan(result));
	return result;
};

transition sineInOut = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto result = -(delta / 2) * (cos(M_PI * dt) - 1);

	Ensures(!std::isnan(result));
	return result;
};

transition halfSine = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto result = delta * sin(M_PI * dt / 2);

	Ensures(!std::isnan(result));
	return result;
};

transition easeOutBack = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	static constexpr auto s = 1.70158;

	const auto t = dt - 1;
	Assert(!std::isnan(t));
	const auto result = delta * (t * t * ((s + 1) * t + s) + 1);

	Ensures(!std::isnan(result));
	return result;
};

transition easeInCirc = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto result = -delta * (sqrt(1 - dt * dt) - 1);

	Ensures(!std::isnan(result));
	return result;
};

transition easeOutCirc = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto t = dt - 1;
	Assert(!std::isnan(t));
	const auto result = delta * sqrt(1 - t * t);

	Ensures(!std::isnan(result));
	return result;
};

transition easeInCubic = [](const float64 &delta, const float64 &dt) {
	const auto result = delta * dt * dt * dt;

	Ensures(!std::isnan(result));
	return result;
};

transition easeOutCubic = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto t = dt - 1;
	Assert(!std::isnan(t));
	const auto result = delta * (t * t * t + 1);

	Ensures(!std::isnan(result));
	return result;
};

transition easeInQuint = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto t2 = dt * dt;
	Assert(!std::isnan(t2));
	const auto result = delta * t2 * t2 * dt;

	Ensures(!std::isnan(result));
	return result;
};

transition easeOutQuint = [](const float64 &delta, const float64 &dt) {
	Expects(!std::isnan(delta));
	Expects(!std::isnan(dt));

	const auto t = dt - 1, t2 = t * t;
	Assert(!std::isnan(t));
	Assert(!std::isnan(t2));
	const auto result = delta * (t2 * t2 * t + 1);

	Ensures(!std::isnan(result));
	return result;
};

rpl::producer<bool> Disables() {
	return AnimationsDisabled.value();
};

bool Disabled() {
	return AnimationsDisabled.current();
}

void SetDisabled(bool disabled) {
	AnimationsDisabled = disabled;
}

int SlowMultiplier() {
	return (SlowMultiplierMinusOne + 1);
}

void SetSlowMultiplier(int multiplier) {
	Expects(multiplier > 0);

	SlowMultiplierMinusOne = multiplier - 1;
}

void DrawStaticLoading(
		QPainter &p,
		QRectF rect,
		float64 stroke,
		QPen pen,
		QBrush brush) {
	PainterHighQualityEnabler hq(p);

	p.setBrush(brush);
	pen.setWidthF(stroke);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.drawEllipse(rect);

	const auto center = rect.center();
	const auto first = QPointF(center.x(), rect.y() + 1.5 * stroke);
	const auto delta = center.y() - first.y();
	const auto second = QPointF(center.x() + delta * 2 / 3., center.y());
	if (delta > 0) {
		QPainterPath path;
		path.moveTo(first);
		path.lineTo(center);
		path.lineTo(second);
		p.drawPath(path);
	}
}

} // anim
