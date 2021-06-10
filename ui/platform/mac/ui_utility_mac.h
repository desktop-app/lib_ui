// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_utility.h"
#include "base/platform/base_platform_info.h"
#include <QtCore/QPoint>

namespace Ui {
namespace Platform {

inline bool TranslucentWindowsSupported(QPoint globalPosition) {
	return true;
}

inline void UpdateOverlayed(not_null<QWidget*> widget) {
}

inline void ClearTransientParent(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return ::Platform::IsMacStoreBuild();
}

inline bool WindowExtentsSupported() {
	return false;
}

inline void SetWindowExtents(QWindow *window, const QMargins &extents) {
}

inline void UnsetWindowExtents(QWindow *window) {
}

inline bool ShowWindowMenu(QWindow *window) {
	return false;
}

} // namespace Platform
} // namespace Ui
