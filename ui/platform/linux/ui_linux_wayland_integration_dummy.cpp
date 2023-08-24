/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/linux/ui_linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"

namespace Ui {
namespace Platform {

struct WaylandIntegration::Private {
};

WaylandIntegration::WaylandIntegration() {
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!::Platform::IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

bool WaylandIntegration::xdgDecorationSupported() {
	return false;
}

void WaylandIntegration::showWindowMenu(
		not_null<QWidget*> widget,
		const QPoint &point) {
}

} // namespace Platform
} // namespace Ui
