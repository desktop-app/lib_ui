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

namespace details {

template <typename T, typename Derived>
class gradients {
public:
	gradients(base::flat_map<T, std::vector<QColor>> colors)
	: _colors(colors) {
		Expects(_colors.size() > 0);
	}

	QGradient gradient(T state1, T state2, float64 b_ratio) const {
		if (b_ratio == 0.) {
			return _gradients.find(state1)->second;
		} else if (b_ratio == 1.) {
			return _gradients.find(state2)->second;
		}

		auto gradient = static_cast<const Derived*>(this)->empty_gradient();
		const auto size = _colors.front().second.size();
		const auto colors1 = _colors.find(state1);
		const auto colors2 = _colors.find(state2);

		Assert(colors1 != end(_colors));
		Assert(colors2 != end(_colors));

		for (auto i = 0; i < size; i++) {
			auto c = color(colors1->second[i], colors2->second[i], b_ratio);
			gradient.setColorAt(i / (size - 1), std::move(c));
		}
		return gradient;
	}

protected:
	void cache_gradients() {
		_gradients = base::flat_map<T, QGradient>();
		for (const auto &[key, value] : _colors) {
			_gradients.emplace(key, gradient(value));
		}
	}

private:
	QGradient gradient(const std::vector<QColor> &colors) const {
		auto gradient = static_cast<const Derived*>(this)->empty_gradient();
		const auto size = colors.size();
		for (auto i = 0; i < size; i++) {
			gradient.setColorAt(i / (size - 1), colors[i]);
		}
		return gradient;
	}

	base::flat_map<T, std::vector<QColor>> _colors;
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
