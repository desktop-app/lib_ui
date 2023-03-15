// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_scale.h"

#include "base/assertion.h"

namespace style {
namespace {

int DevicePixelRatioValue = 1;
int ScaleValue = kScaleDefault;

} // namespace

int DevicePixelRatio() {
	return DevicePixelRatioValue;
}

void SetDevicePixelRatio(int ratio) {
	DevicePixelRatioValue = std::clamp(ratio, 1, kScaleMax / kScaleMin);
}

int Scale() {
	return ScaleValue;
}

void SetScale(int scale) {
	Expects(scale != 0);

	ScaleValue = scale;
}

int MaxScaleForRatio(int ratio) {
	Expects(ratio > 0);

	return std::max(kScaleMax / ratio, kScaleAlwaysAllowMax);
}

int CheckScale(int scale) {
	return (scale == kScaleAuto)
		? kScaleAuto
		: std::clamp(scale, kScaleMin, MaxScaleForRatio(DevicePixelRatio()));
}

} // namespace style
