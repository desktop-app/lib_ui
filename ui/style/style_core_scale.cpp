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

// Default 96 = 100% (Windows-style base DPI). Reset by SetMainScreenDpi
// during Sandbox::setupScreenScale; on platforms where logicalBaseDpi is
// different (e.g. macOS uses 72) the value stored here is still in the
// same units as the per-window `systemDpi` passed to ComputeScaleKey
// (both come from QScreen::logicalDotsPerInch()), so the ratio is the
// platform-agnostic part.
int MainScreenDpiValue = 96;

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

int MainScreenDpi() {
	return MainScreenDpiValue;
}

void SetMainScreenDpi(int dpi) {
	Expects(dpi > 0);

	MainScreenDpiValue = dpi;
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
