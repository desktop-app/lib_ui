// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtCore/QPoint>

namespace Ui {
namespace Platform {

inline bool TranslucentWindowsSupported(QPoint globalPosition) {
	return true;
}

inline void UpdateOverlayed(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return false;
}

} // namespace Platform
} // namespace Ui
