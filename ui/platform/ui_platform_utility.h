// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_window_title.h"

class QPoint;
class QPainter;
class QPaintEvent;

namespace Ui {
namespace Platform {

[[nodiscard]] bool IsApplicationActive();

[[nodiscard]] bool TranslucentWindowsSupported(QPoint globalPosition);

void InitOnTopPanel(not_null<QWidget*> panel);
void DeInitOnTopPanel(not_null<QWidget*> panel);
void ReInitOnTopPanel(not_null<QWidget*> panel);

void UpdateOverlayed(not_null<QWidget*> widget);
void ShowOverAll(not_null<QWidget*> widget, bool canFocus = true);
void BringToBack(not_null<QWidget*> widget);
void IgnoreAllActivation(not_null<QWidget*> widget);
void ClearTransientParent(not_null<QWidget*> widget);

[[nodiscard]] std::optional<bool> IsOverlapped(
    not_null<QWidget*> widget,
    const QRect &rect);

[[nodiscard]] constexpr bool UseMainQueueGeneric();
void DrainMainQueue(); // Needed only if UseMainQueueGeneric() is false.

[[nodiscard]] bool WindowExtentsSupported();
void SetWindowExtents(QWindow *window, const QMargins &extents);
void UnsetWindowExtents(QWindow *window);
bool ShowWindowMenu(QWindow *window);

[[nodiscard]] TitleControls::Layout TitleControlsLayout();
[[nodiscard]] rpl::producer<> TitleControlsLayoutChanged();
void NotifyTitleControlsLayoutChanged();

} // namespace Platform
} // namespace Ui

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "ui/platform/mac/ui_utility_mac.h"
#elif defined Q_OS_UNIX // Q_OS_MAC
#include "ui/platform/linux/ui_utility_linux.h"
#elif defined Q_OS_WINRT || defined Q_OS_WIN // Q_OS_MAC || Q_OS_UNIX
#include "ui/platform/win/ui_utility_win.h"
#endif // Q_OS_MAC || Q_OS_UNIX || Q_OS_WINRT || Q_OS_WIN
