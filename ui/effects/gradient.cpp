// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/gradient.h"

namespace anim {

QColor gradient_color_at(const QGradientStops &stops, float64 ratio) {
	for (auto i = 1; i < stops.size(); i++) {
		const auto currentPoint = stops[i].first;
		const auto previousPoint = stops[i - 1].first;

		if ((ratio <= currentPoint) && (ratio >= previousPoint)) {
			return anim::color(
				stops[i - 1].second,
				stops[i].second,
				(ratio - previousPoint) / (currentPoint - previousPoint));
		}
	}
	return QColor();
}

QColor gradient_color_at(const QGradient &gradient, float64 ratio) {
	return gradient_color_at(gradient.stops(), ratio);
}

} // namespace anim
