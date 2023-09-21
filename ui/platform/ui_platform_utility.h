// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

class QPoint;
class QPainter;
class QPaintEvent;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::Platform {

[[nodiscard]] bool IsApplicationActive();

[[nodiscard]] bool TranslucentWindowsSupported();

void InitOnTopPanel(not_null<QWidget*> panel);
void DeInitOnTopPanel(not_null<QWidget*> panel);
void ReInitOnTopPanel(not_null<QWidget*> panel);

void UpdateOverlayed(not_null<QWidget*> widget);
void ShowOverAll(not_null<QWidget*> widget, bool canFocus = true);
void IgnoreAllActivation(not_null<QWidget*> widget);
void ClearTransientParent(not_null<QWidget*> widget);

void DisableSystemWindowResize(not_null<QWidget*> widget, QSize ratio);

[[nodiscard]] std::optional<bool> IsOverlapped(
    not_null<QWidget*> widget,
    const QRect &rect);

[[nodiscard]] constexpr bool UseMainQueueGeneric();
void DrainMainQueue(); // Needed only if UseMainQueueGeneric() is false.

[[nodiscard]] bool WindowMarginsSupported();
void SetWindowMargins(not_null<QWidget*> widget, const QMargins &margins);
void UnsetWindowMargins(not_null<QWidget*> widget);
void ShowWindowMenu(not_null<QWidget*> widget, const QPoint &point);

void FixPopupMenuNativeEmojiPopup(not_null<PopupMenu*> menu);

} // namespace Ui::Platform

// Platform dependent implementations.

#if defined Q_OS_WINRT || defined Q_OS_WIN
#include "ui/platform/win/ui_utility_win.h"
#elif defined Q_OS_MAC // Q_OS_WINRT || Q_OS_WIN
#include "ui/platform/mac/ui_utility_mac.h"
#else // Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC
#include "ui/platform/linux/ui_utility_linux.h"
#endif // else for Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC
