// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_scale.h"

#include "base/assertion.h"
#include "base/platform/base_platform_info.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <qpa/qplatformscreen.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

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

int BaseDpi() {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	if (const auto screen = QGuiApplication::primaryScreen()) {
		const auto pair = screen->handle()->logicalBaseDpi();
		return int(base::SafeRound((pair.first + pair.second) * 0.5));
	}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return Platform::IsMac() ? 72 : 96;
}

} // namespace style
