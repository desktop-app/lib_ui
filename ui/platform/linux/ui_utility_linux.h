// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_utility.h"

class QPainter;
class QPaintEvent;

namespace Ui {
namespace Platform {

inline void InitOnTopPanel(not_null<QWidget*> panel) {
}

inline void DeInitOnTopPanel(not_null<QWidget*> panel) {
}

inline void ReInitOnTopPanel(not_null<QWidget*> panel) {
}

inline void UpdateOverlayed(not_null<QWidget*> widget) {
}

inline void ShowOverAll(not_null<QWidget*> widget, bool canFocus) {
}

inline void BringToBack(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return true;
}

} // namespace Platform
} // namespace Ui
