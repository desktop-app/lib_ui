/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/linux/ui_linux_wayland_integration.h"

#include "base/platform/base_platform_info.h"
#include "waylandshells/xdg_shell.h"
#include "qwayland-xdg-shell.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>

// private QtWaylandClient headers are using keywords :(
#ifdef QT_NO_KEYWORDS
#define signals Q_SIGNALS
#define slots Q_SLOTS
#endif // QT_NO_KEYWORDS

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandwindow_p.h>
#include <private/qwaylandinputdevice_p.h>

#include <connection_thread.h>
#include <registry.h>

Q_DECLARE_METATYPE(QMargins);

using QtWaylandClient::QWaylandWindow;
using namespace KWayland::Client;

namespace Ui {
namespace Platform {

struct WaylandIntegration::Private {
	std::unique_ptr<ConnectionThread> connection;
	Registry registry;
	QEventLoop interfacesLoop;
	bool interfacesAnnounced = false;
};

WaylandIntegration::WaylandIntegration()
: _private(std::make_unique<Private>()) {
	_private->connection = std::unique_ptr<ConnectionThread>{
		ConnectionThread::fromApplication(),
	};

	_private->registry.create(_private->connection.get());
	_private->registry.setup();

	QObject::connect(
		_private->connection.get(),
		&ConnectionThread::connectionDied,
		&_private->registry,
		&Registry::destroy);

	QObject::connect(
		&_private->registry,
		&Registry::interfacesAnnounced,
		[=] {
			_private->interfacesAnnounced = true;
			if (_private->interfacesLoop.isRunning()) {
				_private->interfacesLoop.quit();
			}
		});
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
	return _private->registry.hasInterface(
		Registry::Interface::XdgDecorationUnstableV1);
}

bool WaylandIntegration::windowExtentsSupported() {
	return WaylandShells::XdgShell();
}

void WaylandIntegration::setWindowExtents(
		QWindow *window,
		const QMargins &extents) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	native->setWindowProperty(
		window->handle(),
		"_desktopApp_waylandCustomMargins",
		QVariant::fromValue<QMargins>(extents));
}

void WaylandIntegration::unsetWindowExtents(QWindow *window) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return;
	}

	native->setWindowProperty(
		window->handle(),
		"_desktopApp_waylandCustomMargins",
		QVariant());
}

bool WaylandIntegration::showWindowMenu(QWindow *window) {
	const auto native = QGuiApplication::platformNativeInterface();
	if (!native) {
		return false;
	}

	const auto toplevel = reinterpret_cast<xdg_toplevel*>(
		native->nativeResourceForWindow(QByteArray("xdg_toplevel"), window));

	const auto seat = reinterpret_cast<wl_seat*>(
		native->nativeResourceForIntegration(QByteArray("wl_seat")));
	
	const auto serial = [&]() -> std::optional<uint32_t> {
		const auto waylandWindow = static_cast<QWaylandWindow*>(
			window->handle());
		if (!waylandWindow) {
			return std::nullopt;
		}
		return waylandWindow->display()->defaultInputDevice()->serial();
	}();

	if (!toplevel || !seat || !serial) {
		return false;
	}

	const auto pos = window->mapFromGlobal(QCursor::pos())
		* window->devicePixelRatio();

	xdg_toplevel_show_window_menu(toplevel, seat, *serial, pos.x(), pos.y());
	return true;
}

} // namespace Platform
} // namespace Ui
