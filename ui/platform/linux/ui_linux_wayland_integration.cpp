/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/linux/ui_linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "qwayland-xdg-shell.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow_p.h>
#include <wayland-client.h>

using namespace QNativeInterface;
using namespace QNativeInterface::Private;

namespace Ui {
namespace Platform {
namespace {

struct WlRegistryDeleter {
	void operator()(wl_registry *value) {
		wl_registry_destroy(value);
	}
};

} // namespace

struct WaylandIntegration::Private {
	std::unique_ptr<wl_registry, WlRegistryDeleter> registry;
	bool xdgDecorationSupported = false;
	uint32_t xdgDecorationName = 0;
	rpl::lifetime lifetime;

	static const struct wl_registry_listener RegistryListener;
};

const struct wl_registry_listener WaylandIntegration::Private::RegistryListener = {
	decltype(wl_registry_listener::global)(+[](
			Private *data,
			wl_registry *registry,
			uint32_t name,
			const char *interface,
			uint32_t version) {
		if (interface == qstr("zxdg_decoration_manager_v1")) {
			data->xdgDecorationSupported = true;
			data->xdgDecorationName = name;
		}
	}),
	decltype(wl_registry_listener::global_remove)(+[](
			Private *data,
			wl_registry *registry,
			uint32_t name) {
		if (name == data->xdgDecorationName) {
			data->xdgDecorationSupported = false;
			data->xdgDecorationName = 0;
		}
	}),
};

WaylandIntegration::WaylandIntegration()
: _private(std::make_unique<Private>()) {
	const auto native = qApp->nativeInterface<QWaylandApplication>();
	if (!native) {
		return;
	}

	const auto display = native->display();
	if (!display) {
		return;
	}

	_private->registry.reset(wl_display_get_registry(display));
	wl_registry_add_listener(
		_private->registry.get(),
		&Private::RegistryListener,
		_private.get());

	wl_display_roundtrip(display);
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!::Platform::IsWayland()) return nullptr;
	static std::optional<WaylandIntegration> instance(std::in_place);
	base::qt_signal_producer(
		QGuiApplication::platformNativeInterface(),
		&QObject::destroyed
	) | rpl::start_with_next([&] {
		instance = std::nullopt;
	}, instance->_private->lifetime);
	if (!instance) return nullptr;
	return &*instance;
}

bool WaylandIntegration::xdgDecorationSupported() {
	return _private->xdgDecorationSupported;
}

bool WaylandIntegration::windowExtentsSupported() {
	QWindow window;
	window.create();
	return window.nativeInterface<QWaylandWindow>();
}

void WaylandIntegration::setWindowExtents(
		not_null<QWidget*> widget,
		const QMargins &extents) {
	const auto window = widget->windowHandle();
	if (!window) {
		return;
	}

	const auto native = window->nativeInterface<QWaylandWindow>();
	if (!native) {
		return;
	}

	native->setCustomMargins(extents);
}

void WaylandIntegration::unsetWindowExtents(not_null<QWidget*> widget) {
	const auto window = widget->windowHandle();
	if (!window) {
		return;
	}

	const auto native = window->nativeInterface<QWaylandWindow>();
	if (!native) {
		return;
	}

	native->setCustomMargins(QMargins());
}

void WaylandIntegration::showWindowMenu(
		not_null<QWidget*> widget,
		const QPoint &point) {
	const auto window = widget->windowHandle();
	if (!window) {
		return;
	}

	const auto native = qApp->nativeInterface<QWaylandApplication>();
	const auto nativeWindow = window->nativeInterface<QWaylandWindow>();
	if (!native || !nativeWindow) {
		return;
	}

	const auto toplevel = nativeWindow->surfaceRole<xdg_toplevel>();
	const auto seat = native->lastInputSeat();
	if (!toplevel || !seat) {
		return;
	}

	xdg_toplevel_show_window_menu(
		toplevel,
		seat,
		native->lastInputSerial(),
		point.x(),
		point.y());
}

} // namespace Platform
} // namespace Ui
