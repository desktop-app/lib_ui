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
class PopupMenu;
} // namespace Ui

namespace Ui::Platform {
namespace internal {

// Actual requestor, cached by the public interface
[[nodiscard]] TitleControls::Layout TitleControlsLayout();
void NotifyTitleControlsLayoutChanged(
    const std::optional<TitleControls::Layout> &layout = std::nullopt);

} // namespace internal

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

[[nodiscard]] bool WindowExtentsSupported();
void SetWindowExtents(not_null<QWidget*> widget, const QMargins &extents);
void UnsetWindowExtents(not_null<QWidget*> widget);
void ShowWindowMenu(not_null<QWidget*> widget, const QPoint &point);

[[nodiscard]] TitleControls::Layout TitleControlsLayout();
[[nodiscard]] rpl::producer<TitleControls::Layout> TitleControlsLayoutValue();
[[nodiscard]] rpl::producer<TitleControls::Layout> TitleControlsLayoutChanged();
[[nodiscard]] bool TitleControlsOnLeft(
		const TitleControls::Layout &layout = TitleControlsLayout()) {
	if (ranges::contains(layout.left, TitleControl::Close)) {
		return true;
	} else if (ranges::contains(layout.right, TitleControl::Close)) {
		return false;
	} else if (layout.left.size() > layout.right.size()) {
		return true;
	}
	return false;
}

void FixPopupMenuNativeEmojiPopup(not_null<PopupMenu*> menu);

// Workaround for a Qt/Wayland bug that hides the parent popup when
// the child popup gets hidden, by sending Deactivate / Activate events.
void RegisterChildPopupHiding();
[[nodiscard]] bool SkipApplicationDeactivateEvent();
void GotApplicationActivateEvent();

} // namespace Ui::Platform

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "ui/platform/mac/ui_utility_mac.h"
#elif defined Q_OS_UNIX // Q_OS_MAC
#include "ui/platform/linux/ui_utility_linux.h"
#elif defined Q_OS_WINRT || defined Q_OS_WIN // Q_OS_MAC || Q_OS_UNIX
#include "ui/platform/win/ui_utility_win.h"
#endif // Q_OS_MAC || Q_OS_UNIX || Q_OS_WINRT || Q_OS_WIN
