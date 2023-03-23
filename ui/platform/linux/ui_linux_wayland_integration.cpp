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
#include <wayland-client.h>

typedef void (*SetWindowMarginsFunc)(
	QWindow *window,
	const QMargins &margins);

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
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	const auto display = reinterpret_cast<wl_display*>(
		native->nativeResourceForIntegration(QByteArray("wl_display")));

	if (!display) {
		return;
	}

	_private->registry.reset(wl_display_get_registry(display));
	wl_registry_add_listener(
		_private->registry.get(),
		&Private::RegistryListener,
		_private.get());

	base::qt_signal_producer(
		native,
		&QObject::destroyed
	) | rpl::start_with_next([=] {
		// too late for standard destructors, just free
		free(_private->registry.release());
	}, _private->lifetime);

	wl_display_roundtrip(display);
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!::Platform::IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

bool WaylandIntegration::xdgDecorationSupported() {
	return _private->xdgDecorationSupported;
}

bool WaylandIntegration::windowExtentsSupported() {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return false;
	}

	const auto setWindowMargins = reinterpret_cast<SetWindowMarginsFunc>(
		reinterpret_cast<void*>(
			native->nativeResourceFunctionForWindow("setmargins")));

	if (!setWindowMargins) {
		return false;
	}

	return true;
}

void WaylandIntegration::setWindowExtents(
		not_null<QWidget*> widget,
		const QMargins &extents) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	const auto setWindowMargins = reinterpret_cast<SetWindowMarginsFunc>(
		reinterpret_cast<void*>(
			native->nativeResourceFunctionForWindow("setmargins")));

	if (!setWindowMargins) {
		return;
	}

	setWindowMargins(widget->windowHandle(), extents);
}

void WaylandIntegration::unsetWindowExtents(not_null<QWidget*> widget) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	const auto setWindowMargins = reinterpret_cast<SetWindowMarginsFunc>(
		reinterpret_cast<void*>(
			native->nativeResourceFunctionForWindow("setmargins")));

	if (!setWindowMargins) {
		return;
	}

	setWindowMargins(widget->windowHandle(), QMargins());
}

void WaylandIntegration::showWindowMenu(
		not_null<QWidget*> widget,
		const QPoint &point) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	const auto toplevel = reinterpret_cast<xdg_toplevel*>(
		native->nativeResourceForWindow(
			QByteArray("xdg_toplevel"),
			widget->windowHandle()));

	const auto seat = reinterpret_cast<wl_seat*>(
		native->nativeResourceForIntegration(QByteArray("wl_seat")));

	const auto serial = uint32_t(reinterpret_cast<quintptr>(
		native->nativeResourceForIntegration(QByteArray("serial"))));

	if (!toplevel || !seat) {
		return;
	}

	xdg_toplevel_show_window_menu(
		toplevel,
		seat,
		serial,
		point.x(),
		point.y());
}

} // namespace Platform
} // namespace Ui
