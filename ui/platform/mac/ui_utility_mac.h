// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/platform/base_platform_info.h"
#include <QtCore/QPoint>

namespace Ui {
namespace Platform {

inline bool TranslucentWindowsSupported(QPoint globalPosition) {
	return true;
}

inline void UpdateOverlayed(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return ::Platform::IsMacStoreBuild();
}

} // namespace Platform
} // namespace Ui
