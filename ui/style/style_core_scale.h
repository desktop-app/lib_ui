// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/algorithm.h"
#include "base/assertion.h"

#include <QtCore/QSize>

#include <algorithm>
#include <cmath>

namespace style {

inline constexpr auto kScaleAuto = 0;
inline constexpr auto kScaleMin = 50;
inline constexpr auto kScaleDefault = 100;
inline constexpr auto kScaleMax = 300;
inline constexpr auto kScaleAlwaysAllowMax = 200;

[[nodiscard]] int DevicePixelRatio();
void SetDevicePixelRatio(int ratio);

[[nodiscard]] int Scale();
void SetScale(int scale);

[[nodiscard]] int MaxScaleForRatio(int ratio);
[[nodiscard]] int CheckScale(int scale);

template <typename T>
[[nodiscard]] inline T ConvertScale(T value, int scale) {
	if (value < 0.) {
		Assert(!(T(-value) < 0.)); // T = int, value = INT_MIN.
		return -ConvertScale(-value, scale);
	}
	const auto result = T(base::SafeRound(
		(double(value) * scale / 100.) - 0.01));
	return (!std::is_integral_v<T> || !value || result) ? result : 1;
}

template <typename T>
[[nodiscard]] inline T ConvertScale(T value) {
	return ConvertScale(value, Scale());
}

template <typename T>
[[nodiscard]] inline T ConvertScaleExact(T value, int scale) {
	return (value < 0.)
		? (-ConvertScale(-value, scale))
		: T(double(value) * scale / 100.);
}

template <typename T>
[[nodiscard]] inline T ConvertScaleExact(T value) {
	return ConvertScaleExact(value, Scale());
}

[[nodiscard]] inline QSize ConvertScale(QSize size) {
	return QSize(ConvertScale(size.width()), ConvertScale(size.height()));
}

} // namespace style
