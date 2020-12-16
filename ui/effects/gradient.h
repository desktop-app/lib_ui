// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flat_map.h"
#include "ui/effects/animation_value.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QRadialGradient>

namespace anim {

struct gradient_colors {
	explicit gradient_colors(QColor color) {
		stops.push_back({ 0., color });
		stops.push_back({ 1., color });
	}
	explicit gradient_colors(std::vector<QColor> colors) {
		if (colors.size() == 1) {
			gradient_colors(colors.front());
			return;
		}
		const auto last = float(colors.size() - 1);
		for (auto i = 0; i < colors.size(); i++) {
			stops.push_back({ i / last, std::move(colors[i]) });
		}
	}
	explicit gradient_colors(QGradientStops colors)
	: stops(std::move(colors)) {
	}

	QGradientStops stops;
};

namespace details {

template <typename T, typename Derived>
class gradients {
public:
	gradients(base::flat_map<T, std::vector<QColor>> colors) {
		Expects(colors.size() > 0);

		for (const auto &[key, value] : colors) {
			auto c = gradient_colors(std::move(value));
			_gradients.emplace(key, gradient_with_stops(std::move(c.stops)));
		}
	}
	gradients(base::flat_map<T, gradient_colors> colors) {
		Expects(colors.size() > 0);

		for (const auto &[key, c] : colors) {
			_gradients.emplace(key, gradient_with_stops(std::move(c.stops)));
		}
	}

	QGradient gradient(T state1, T state2, float64 b_ratio) const {
		if (b_ratio == 0.) {
			return _gradients.find(state1)->second;
		} else if (b_ratio == 1.) {
			return _gradients.find(state2)->second;
		}

		auto gradient = empty_gradient();
		const auto gradient1 = _gradients.find(state1);
		const auto gradient2 = _gradients.find(state2);

		Assert(gradient1 != end(_gradients));
		Assert(gradient2 != end(_gradients));

		const auto stops1 = gradient1->second.stops();
		const auto stops2 = gradient2->second.stops();

		const auto size = stops1.size();

		for (auto i = 0; i < size; i++) {
			auto c = color(stops1[i].second, stops2[i].second, b_ratio);
			gradient.setColorAt(i / (size - 1), std::move(c));
		}
		return gradient;
	}

protected:
	void cache_gradients() {
		auto copy = std::move(_gradients);
		for (const auto &[key, value] : copy) {
			_gradients.emplace(key, gradient_with_stops(value.stops()));
		}
	}

private:
	QGradient empty_gradient() const {
		return static_cast<const Derived*>(this)->empty_gradient();
	}
	QGradient gradient_with_stops(QGradientStops stops) const {
		auto gradient = empty_gradient();
		gradient.setStops(std::move(stops));
		return gradient;
	}

	base::flat_map<T, QGradient> _gradients;

};

} // namespace details

template <typename T>
class linear_gradients final
	: public details::gradients<T, linear_gradients<T>> {
	using parent = details::gradients<T, linear_gradients<T>>;

public:
	linear_gradients(
		base::flat_map<T, std::vector<QColor>> colors,
		QPointF point1,
		QPointF point2)
	: parent(std::move(colors)) {
		set_points(point1, point2);
	}
	linear_gradients(
		base::flat_map<T, gradient_colors> colors,
		QPointF point1,
		QPointF point2)
	: parent(std::move(colors)) {
		set_points(point1, point2);
	}

	void set_points(QPointF point1, QPointF point2) {
		if (_point1 == point1 && _point2 == point2) {
			return;
		}
		_point1 = point1;
		_point2 = point2;
		parent::cache_gradients();
	}

private:
	friend class details::gradients<T, linear_gradients<T>>;

	QGradient empty_gradient() const {
		return QLinearGradient(_point1, _point2);
	}

	QPointF _point1;
	QPointF _point2;

};

template <typename T>
class radial_gradients final
	: public details::gradients<T, radial_gradients<T>> {
	using parent = details::gradients<T, radial_gradients<T>>;

public:
	radial_gradients(
		base::flat_map<T, std::vector<QColor>> colors,
		QPointF center,
		float radius)
	: parent(std::move(colors)) {
		set_points(center, radius);
	}
	radial_gradients(
		base::flat_map<T, gradient_colors> colors,
		QPointF center,
		float radius)
	: parent(std::move(colors)) {
		set_points(center, radius);
	}

	void set_points(QPointF center, float radius) {
		if (_center == center && _radius == radius) {
			return;
		}
		_center = center;
		_radius = radius;
		parent::cache_gradients();
	}

private:
	friend class details::gradients<T, radial_gradients<T>>;

	QGradient empty_gradient() const {
		return QRadialGradient(_center, _radius);
	}

	QPointF _center;
	float _radius = 0.;

};

} // namespace anim
