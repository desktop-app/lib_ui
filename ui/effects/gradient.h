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

class linear_gradient {
public:
	linear_gradient(
		std::vector<QColor> colors_from,
		std::vector<QColor> colors_to,
		QPointF point1,
		QPointF point2)
	: _colors_from(colors_from)
	, _colors_to(colors_to)
	, _point1(point1)
	, _point2(point2)
	, _gradient_from(gradient(colors_from))
	, _gradient_to(gradient(colors_to)) {
		Expects(colors_from.size() == colors_to.size());
	}

	QLinearGradient gradient(float64 b_ratio) const {
		if (b_ratio == 0.) {
			return _gradient_from;
		} else if (b_ratio == 1.) {
			return _gradient_to;
		}
		auto colors = std::vector<QColor>(_colors_to.size());
		for (auto i = 0; i < colors.size(); i++) {
			colors[i] = color(_colors_from[i], _colors_to[i], b_ratio);
		}
		return gradient(colors);
	}

private:
	QLinearGradient gradient(const std::vector<QColor> &colors) const {
		auto gradient = QLinearGradient(_point1, _point2);
		const auto size = colors.size();
		for (auto i = 0; i < size; i++) {
			gradient.setColorAt(i / (size - 1), colors[i]);
		}
		return gradient;
	}

	std::vector<QColor> _colors_from;
	std::vector<QColor> _colors_to;
	QPointF _point1;
	QPointF _point2;

	QLinearGradient _gradient_from;
	QLinearGradient _gradient_to;

};

template <typename T>
class linear_gradients {
public:
	linear_gradients(
		base::flat_map<T, std::vector<QColor>> colors,
		QPointF point1,
		QPointF point2)
	: _colors(colors)
	, _point1(point1)
	, _point2(point2) {
		Expects(_colors.size() > 0);

		cache_gradients();
	}

	QLinearGradient gradient(T state1, T state2, float64 b_ratio) const {
		if (b_ratio == 0.) {
			return _gradients.find(state1)->second;
		} else if (b_ratio == 1.) {
			return _gradients.find(state2)->second;
		}

		auto gradient = QLinearGradient(_point1, _point2);
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

	void set_points(QPointF point1, QPointF point2) {
		if (_point1 == point1 && _point2 == point2) {
			return;
		}
		_point1 = point1;
		_point2 = point2;
		cache_gradients();
	}

private:
	void cache_gradients() {
		_gradients = base::flat_map<T, QLinearGradient>();
		for (const auto &[key, value] : _colors) {
			_gradients.emplace(key, gradient(value));
		}
	}

	QLinearGradient gradient(const std::vector<QColor> &colors) const {
		auto gradient = QLinearGradient(_point1, _point2);
		const auto size = colors.size();
		for (auto i = 0; i < size; i++) {
			gradient.setColorAt(i / (size - 1), colors[i]);
		}
		return gradient;
	}

	base::flat_map<T, std::vector<QColor>> _colors;
	QPointF _point1;
	QPointF _point2;

	base::flat_map<T, QLinearGradient> _gradients;

};

class radial_gradient {
public:
	radial_gradient(
		std::vector<QColor> colors_from,
		std::vector<QColor> colors_to,
		QPointF center,
		float radius)
	: _colors_from(colors_from)
	, _colors_to(colors_to)
	, _center(center)
	, _radius(radius)
	, _gradient_from(gradient(colors_from))
	, _gradient_to(gradient(colors_to)) {
		Expects(colors_from.size() == colors_to.size());
	}

	QRadialGradient gradient(float64 b_ratio) const {
		if (b_ratio == 0.) {
			return _gradient_from;
		} else if (b_ratio == 1.) {
			return _gradient_to;
		}
		auto colors = std::vector<QColor>(_colors_to.size());
		for (auto i = 0; i < colors.size(); i++) {
			colors[i] = color(_colors_from[i], _colors_to[i], b_ratio);
		}
		return gradient(colors);
	}

private:
	QRadialGradient gradient(const std::vector<QColor> &colors) const {
		auto gradient = QRadialGradient(_center, _radius);
		const auto size = colors.size();
		for (auto i = 0; i < size; i++) {
			gradient.setColorAt(i / (size - 1), colors[i]);
		}
		return gradient;
	}

	std::vector<QColor> _colors_from;
	std::vector<QColor> _colors_to;
	QPointF _center;
	float _radius;

	QRadialGradient _gradient_from;
	QRadialGradient _gradient_to;

};

template <typename T>
class radial_gradients {
public:
	radial_gradients(
		base::flat_map<T, std::vector<QColor>> colors,
		QPointF center,
		float radius)
	: _colors(colors)
	, _center(center)
	, _radius(radius) {
		Expects(_colors.size() > 0);

		cache_gradients();
	}

	QRadialGradient gradient(T state1, T state2, float64 b_ratio) const {
		if (b_ratio == 0.) {
			return _gradients.find(state1)->second;
		} else if (b_ratio == 1.) {
			return _gradients.find(state2)->second;
		}

		auto gradient = QRadialGradient(_center, _radius);
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

	void set_points(QPointF center, float radius) {
		if (_center == center && _radius == radius) {
			return;
		}
		_center = center;
		_radius = radius;
		cache_gradients();
	}

private:
	void cache_gradients() {
		_gradients = base::flat_map<T, QRadialGradient>();
		for (const auto &[key, value] : _colors) {
			_gradients.emplace(key, gradient(value));
		}
	}

	QRadialGradient gradient(const std::vector<QColor> &colors) const {
		auto gradient = QRadialGradient(_center, _radius);
		const auto size = colors.size();
		for (auto i = 0; i < size; i++) {
			gradient.setColorAt(i / (size - 1), colors[i]);
		}
		return gradient;
	}

	base::flat_map<T, std::vector<QColor>> _colors;
	QPointF _center;
	float _radius;

	base::flat_map<T, QRadialGradient> _gradients;

};

} // namespace anim
