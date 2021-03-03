/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/linux/ui_linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"

#include <QtGui/QWindow>

// private headers are using keywords :(
#ifdef QT_NO_KEYWORDS
#define signals Q_SIGNALS
#define slots Q_SLOTS
#endif // QT_NO_KEYWORDS

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandwindow_p.h>
#include <private/qwaylandshellsurface_p.h>

using QtWaylandClient::QWaylandWindow;

namespace Ui {
namespace Platform {

WaylandIntegration::WaylandIntegration() {
}

WaylandIntegration *WaylandIntegration::Instance() {
	if (!::Platform::IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

bool WaylandIntegration::showWindowMenu(QWindow *window) {
	if (const auto waylandWindow = static_cast<QWaylandWindow*>(
		window->handle())) {
		if (const auto seat = waylandWindow->display()->lastInputDevice()) {
			if (const auto shellSurface = waylandWindow->shellSurface()) {
				return shellSurface->showWindowMenu(seat);
			}
		}
	}

	return false;
}

} // namespace Platform
} // namespace Ui
