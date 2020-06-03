// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/linux/ui_utility_linux.h"

#include "base/flat_set.h"
#include "ui/ui_log.h"
#include "base/platform/base_platform_info.h"

#include <QtCore/QPoint>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <qpa/qplatformnativeinterface.h>

namespace Ui {
namespace Platform {

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
}

bool TranslucentWindowsSupported(QPoint globalPosition) {
	if (::Platform::IsWayland()) {
		return true;
	}
	if (const auto native = QGuiApplication::platformNativeInterface()) {
		if (const auto desktop = QApplication::desktop()) {
			const auto index = desktop->screenNumber(globalPosition);
			const auto screens = QGuiApplication::screens();
			if (const auto screen = (index >= 0 && index < screens.size()) ? screens[index] : QGuiApplication::primaryScreen()) {
				if (native->nativeResourceForScreen(QByteArray("compositingEnabled"), screen)) {
					return true;
				}
				static auto WarnedAbout = base::flat_set<int>();
				if (!WarnedAbout.contains(index)) {
					WarnedAbout.emplace(index);
					UI_LOG(("WARNING: Compositing is disabled for screen index %1 (for position %2,%3)").arg(index).arg(globalPosition.x()).arg(globalPosition.y()));
				}
			} else {
				UI_LOG(("WARNING: Could not get screen for index %1 (for position %2,%3)").arg(index).arg(globalPosition.x()).arg(globalPosition.y()));
			}
		}
	}
	return false;
}

void IgnoreAllActivation(not_null<QWidget*> widget) {
}

} // namespace Platform
} // namespace Ui
