// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

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

} // namespace anim
