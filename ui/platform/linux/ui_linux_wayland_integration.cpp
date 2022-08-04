/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/linux/ui_linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "waylandshells/xdg_shell.h"
#include "qwayland-xdg-shell.h"

#include <QtCore/QtPlugin>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

// private QtWaylandClient headers are using keywords :(
#ifdef QT_NO_KEYWORDS
#define signals Q_SIGNALS
#define slots Q_SLOTS
#endif // QT_NO_KEYWORDS

#ifndef QT_STATICPLUGIN
#define QT_STATICPLUGIN
#endif // !QT_STATICPLUGIN

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandwindow_p.h>
#include <private/qwaylandinputdevice_p.h>
#include <private/qwaylandclientbufferintegrationplugin_p.h>
#include <private/qwaylandeglclientbufferintegration_p.h>
#include <private/qwaylandeglwindow_p.h>
#include <private/qwidgetwindow_p.h>
#include <private/qwidget_p.h>

Q_DECLARE_METATYPE(QMargins);

Q_IMPORT_PLUGIN(DesktopAppWaylandEglClientBufferPlugin)

using namespace QtWaylandClient;

namespace Ui {
namespace Platform {
namespace {

struct WlRegistryDeleter {
	void operator()(wl_registry *value) {
		wl_registry_destroy(value);
	}
};

struct WlCallbackDeleter {
	void operator()(wl_callback *value) {
		wl_callback_destroy(value);
	}
};

class DesktopAppWaylandEglWindow : public QWaylandEglWindow {
public:
	using QWaylandEglWindow::QWaylandEglWindow;

	void ensureSize() override {
		_settingGeometry = false;
		QWaylandEglWindow::ensureSize();
	}

	QRect geometry() const override {
		if (!_settingGeometry) {
			if (const auto widgetWindow = qobject_cast<const QWidgetWindow*>(
				window())) {
				return widgetWindow->widget()->geometry();
			}
		}
		return QWaylandEglWindow::geometry();
	}

	void setGeometry(const QRect &rect) override {
		_settingGeometry = true;
		QWaylandEglWindow::setGeometry(rect);
	}

private:
	bool _settingGeometry = false;
};

class DesktopAppWaylandEglClientBufferIntegration
	: public QWaylandEglClientBufferIntegration {
public:
	void initialize(QWaylandDisplay *display) override {
		m_display = display;
		QWaylandEglClientBufferIntegration::initialize(display);
	}

	QWaylandWindow *createEglWindow(QWindow *window) override {
		return new DesktopAppWaylandEglWindow(window, m_display);
	}

private:
	QWaylandDisplay *m_display = nullptr;
};

class DesktopAppWaylandEglClientBufferPlugin
	: public QWaylandClientBufferIntegrationPlugin {
	Q_OBJECT
	Q_PLUGIN_METADATA(
		IID QWaylandClientBufferIntegrationFactoryInterface_iid
		FILE "wayland-egl.json")
public:
	QWaylandClientBufferIntegration *create(
			const QString&,
			const QStringList&) override {
		return new DesktopAppWaylandEglClientBufferIntegration();
	}
};

} // namespace

struct WaylandIntegration::Private {
	std::unique_ptr<wl_registry, WlRegistryDeleter> registry;
	std::unique_ptr<wl_callback, WlCallbackDeleter> callback;
	QEventLoop interfacesLoop;
	bool interfacesAnnounced = false;
	bool xdgDecorationSupported = false;
	uint32_t xdgDecorationName = 0;
	rpl::lifetime lifetime;

	static const struct wl_registry_listener RegistryListener;
	static const struct wl_callback_listener CallbackListener;
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
		}
	}),
	decltype(wl_registry_listener::global_remove)(+[](
			Private *data,
			wl_registry *registry,
			uint32_t name) {
		if (name == data->xdgDecorationName) {
			data->xdgDecorationSupported = false;
		}
	}),
};

const struct wl_callback_listener WaylandIntegration::Private::CallbackListener = {
	decltype(wl_callback_listener::done)(+[](
			Private *data,
			wl_callback *callback,
			uint32_t serial) {
		data->interfacesAnnounced = true;
		if (data->interfacesLoop.isRunning()) {
			data->interfacesLoop.quit();
		}
		data->callback = nullptr;
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
	_private->callback.reset(wl_display_sync(display));

	wl_registry_add_listener(
		_private->registry.get(),
		&Private::RegistryListener,
		_private.get());

	wl_callback_add_listener(
		_private->callback.get(),
		&Private::CallbackListener,
		_private.get());

	base::qt_signal_producer(
		native,
		&QObject::destroyed
	) | rpl::start_with_next([=] {
		// too late for standard destructors, just free
		free(_private->callback.release());
		free(_private->registry.release());
	}, _private->lifetime);
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!::Platform::IsWayland()) return nullptr;
	static WaylandIntegration instance;
	return &instance;
}

void WaylandIntegration::waitForInterfaceAnnounce() {
	Expects(!_private->interfacesLoop.isRunning());
	if (!_private->interfacesAnnounced) {
		_private->interfacesLoop.exec();
	}
}

bool WaylandIntegration::xdgDecorationSupported() {
	return _private->xdgDecorationSupported;
}

bool WaylandIntegration::windowExtentsSupported() {
	return WaylandShells::XdgShell();
}

void WaylandIntegration::setWindowExtents(
		not_null<QWidget*> widget,
		const QMargins &extents) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	native->setWindowProperty(
		widget->windowHandle()->handle(),
		"_desktopApp_waylandCustomMargins",
		QVariant::fromValue<QMargins>(extents));
}

void WaylandIntegration::unsetWindowExtents(not_null<QWidget*> widget) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	native->setWindowProperty(
		widget->windowHandle()->handle(),
		"_desktopApp_waylandCustomMargins",
		QVariant());
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
	
	const auto serial = [&]() -> std::optional<uint32_t> {
		const auto waylandWindow = static_cast<QWaylandWindow*>(
			widget->windowHandle()->handle());
		if (!waylandWindow) {
			return std::nullopt;
		}
		return waylandWindow->display()->defaultInputDevice()->serial();
	}();

	if (!toplevel || !seat || !serial) {
		return;
	}

	xdg_toplevel_show_window_menu(
		toplevel,
		seat,
		*serial,
		point.x(),
		point.y());
}

} // namespace Platform
} // namespace Ui

#include "ui/platform/linux/ui_linux_wayland_integration.moc"
