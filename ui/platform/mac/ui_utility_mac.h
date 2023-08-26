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

inline bool TranslucentWindowsSupported() {
	return true;
}

inline void UpdateOverlayed(not_null<QWidget*> widget) {
}

inline void ClearTransientParent(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return ::Platform::IsMacStoreBuild();
}

inline bool WindowMarginsSupported() {
	return false;
}

inline void SetWindowMargins(not_null<QWidget*> widget, const QMargins &margins) {
}

inline void UnsetWindowMargins(not_null<QWidget*> widget) {
}

inline void ShowWindowMenu(not_null<QWidget*> widget, const QPoint &point) {
}

inline void FixPopupMenuNativeEmojiPopup(not_null<PopupMenu*> menu) {
}

} // namespace Platform
} // namespace Ui
