// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_utility.h"

#include <QtCore/QPoint>

class QPainter;
class QPaintEvent;

namespace Ui {
namespace Platform {

inline bool TranslucentWindowsSupported(QPoint globalPosition) {
	return true;
}

inline void InitOnTopPanel(not_null<QWidget*> panel) {
}

inline void DeInitOnTopPanel(not_null<QWidget*> panel) {
}

inline void ReInitOnTopPanel(not_null<QWidget*> panel) {
}

inline void StartTranslucentPaint(QPainter &p, const QRegion &region) {
}

inline void ShowOverAll(not_null<QWidget*> widget, bool canFocus) {
}

inline void BringToBack(not_null<QWidget*> widget) {
}

inline void ClearTransientParent(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return true;
}

inline bool WindowExtentsSupported() {
	return false;
}

inline bool SetWindowExtents(QWindow *window, const QMargins &extents) {
	return false;
}

inline bool UnsetWindowExtents(QWindow *window) {
	return false;
}

} // namespace Platform
} // namespace Ui
