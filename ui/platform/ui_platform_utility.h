// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

#include <QtCore/QRect>
#include <QtCore/QString>

#include <cstdint>
#include <optional>

class QPoint;
class QPainter;
class QPaintEvent;
class QWidget;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::Platform {

[[nodiscard]] bool IsApplicationActive();

[[nodiscard]] bool TranslucentWindowsSupported();

void InitOnTopPanel(not_null<QWidget*> panel);
void DeInitOnTopPanel(not_null<QWidget*> panel);
void ReInitOnTopPanel(not_null<QWidget*> panel);

void ShowOverAll(not_null<QWidget*> widget, bool canFocus = true);
void IgnoreAllActivation(not_null<QWidget*> widget);
void ClearTransientParent(not_null<QWidget*> widget);
struct ForeignParent {
	enum class Type {
		None,
		X11,
		Wayland,
	};

	Type type = Type::None;
	uintptr_t x11 = 0;
	QString wayland;

	[[nodiscard]] explicit operator bool() const {
		return ((type == Type::X11) && x11)
			|| ((type == Type::Wayland) && !wayland.isEmpty());
	}
};
[[nodiscard]] std::optional<QRect> ForeignWindowGeometry(
	const ForeignParent &parent);
void SetForeignTransientParent(
	not_null<QWidget*> widget,
	const ForeignParent &parent);
void AcceptAllMouseInput(not_null<QWidget*> widget);

void DisableSystemWindowResize(not_null<QWidget*> widget, QSize ratio);

[[nodiscard]] std::optional<bool> IsOverlapped(
	not_null<QWidget*> widget,
	const QRect &rect);

[[nodiscard]] constexpr bool UseMainQueueGeneric();
void DrainMainQueue(); // Needed only if UseMainQueueGeneric() is false.

[[nodiscard]] bool WindowMarginsSupported();
void SetWindowMargins(not_null<QWidget*> widget, const QMargins &margins);
void ShowWindowMenu(not_null<QWidget*> widget, const QPoint &point);

void FixPopupMenuNativeEmojiPopup(not_null<PopupMenu*> menu);

struct SystemTextReplaceResult {
	int length = 0;
	QString replacement;
};
[[nodiscard]] SystemTextReplaceResult FindSystemTextReplace(
	const QString &text);

} // namespace Ui::Platform

// Platform dependent implementations.

#if defined Q_OS_WINRT || defined Q_OS_WIN
#include "ui/platform/win/ui_utility_win.h"
#elif defined Q_OS_MAC // Q_OS_WINRT || Q_OS_WIN
#include "ui/platform/mac/ui_utility_mac.h"
#else // Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC
#include "ui/platform/linux/ui_utility_linux.h"
#endif // else for Q_OS_WINRT || Q_OS_WIN || Q_OS_MAC
