// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace anim {

TG_FORCE_INLINE float64 interpolateF(float a, float b, float64 b_ratio) {
	return a + float64(b - a) * b_ratio;
};

TG_FORCE_INLINE QRectF interpolatedRectF(
		const QRectF &r1,
		const QRectF &r2,
		float64 ratio) {
	return QRectF(
		interpolateF(r1.x(), r2.x(), ratio),
		interpolateF(r1.y(), r2.y(), ratio),
		interpolateF(r1.width(), r2.width(), ratio),
		interpolateF(r1.height(), r2.height(), ratio));
}

} // namespace anim
