// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/linux/ui_utility_linux.h"

#include "ui/ui_log.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_xcb_utilities_linux.h"
#include "ui/platform/linux/ui_linux_wayland_integration.h"
#include "base/const_string.h"
#include "base/qt_adapters.h"
#include "base/flat_set.h"

#include <QtCore/QPoint>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <qpa/qplatformnativeinterface.h>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

Q_DECLARE_METATYPE(QMargins);

namespace Ui {
namespace Platform {
namespace {

constexpr auto kXCBFrameExtentsAtomName = "_GTK_FRAME_EXTENTS"_cs;

constexpr auto kXDGDesktopPortalService = "org.freedesktop.portal.Desktop"_cs;
constexpr auto kXDGDesktopPortalObjectPath = "/org/freedesktop/portal/desktop"_cs;
constexpr auto kSettingsPortalInterface = "org.freedesktop.portal.Settings"_cs;

bool SetXCBFrameExtents(QWindow *window, const QMargins &extents) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName.utf16());

	if (!frameExtentsAtom.has_value()) {
		return false;
	}

	const auto extentsVector = std::vector<uint>{
		uint(extents.left()),
		uint(extents.right()),
		uint(extents.top()),
		uint(extents.bottom()),
	};

	xcb_change_property(
		connection,
		XCB_PROP_MODE_REPLACE,
		window->winId(),
		*frameExtentsAtom,
		XCB_ATOM_CARDINAL,
		32,
		extentsVector.size(),
		extentsVector.data());

	return true;
}

bool UnsetXCBFrameExtents(QWindow *window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName.utf16());

	if (!frameExtentsAtom.has_value()) {
		return false;
	}

	xcb_delete_property(
		connection,
		window->winId(),
		*frameExtentsAtom);

	return true;
}

bool ShowXCBWindowMenu(QWindow *window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return false;
	}

	const auto root = base::Platform::XCB::GetRootWindowFromQt();
	if (!root.has_value()) {
		return false;
	}

	const auto showWindowMenuAtom = base::Platform::XCB::GetAtom(
		connection,
		"_GTK_SHOW_WINDOW_MENU");

	if (!showWindowMenuAtom.has_value()) {
		return false;
	}

	const auto globalPos = QCursor::pos();

	xcb_client_message_event_t xev;
	xev.response_type = XCB_CLIENT_MESSAGE;
	xev.type = *showWindowMenuAtom;
	xev.sequence = 0;
	xev.window = window->winId();
	xev.format = 32;
	xev.data.data32[0] = 0;
	xev.data.data32[1] = globalPos.x();
	xev.data.data32[2] = globalPos.y();
	xev.data.data32[3] = 0;
	xev.data.data32[4] = 0;

	xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
	xcb_send_event(
		connection,
		false,
		*root,
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
		reinterpret_cast<const char*>(&xev));

	return true;
}

TitleControls::Control GtkKeywordToTitleControl(const QString &keyword) {
	if (keyword == qstr("minimize")) {
		return TitleControls::Control::Minimize;
	} else if (keyword == qstr("maximize")) {
		return TitleControls::Control::Maximize;
	} else if (keyword == qstr("close")) {
		return TitleControls::Control::Close;
	}

	return TitleControls::Control::Unknown;
}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
std::optional<TitleControls::Layout> PortalTitleControlsLayout() {
	auto message = QDBusMessage::createMethodCall(
		kXDGDesktopPortalService.utf16(),
		kXDGDesktopPortalObjectPath.utf16(),
		kSettingsPortalInterface.utf16(),
		"Read");

	message.setArguments({
		"org.gnome.desktop.wm.preferences",
		"button-layout"
	});

	const QDBusReply<QVariant> reply = QDBusConnection::sessionBus().call(
		message);

	if (!reply.isValid() || !reply.value().canConvert<QDBusVariant>()) {
		return std::nullopt;
	}

	const auto valueVariant = qvariant_cast<QDBusVariant>(
		reply.value()).variant();

	if (!valueVariant.canConvert<QString>()) {
		return std::nullopt;
	}

	const auto valueBySides = valueVariant.toString().split(':');

	std::vector<TitleControls::Control> controlsLeft;
	ranges::transform(
		valueBySides[0].split(','),
		ranges::back_inserter(controlsLeft),
		GtkKeywordToTitleControl);

	std::vector<TitleControls::Control> controlsRight;
	if (valueBySides.size() > 1) {
		ranges::transform(
			valueBySides[1].split(','),
			ranges::back_inserter(controlsRight),
			GtkKeywordToTitleControl);
	}

	return TitleControls::Layout{
		.left = controlsLeft,
		.right = controlsRight
	};
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

} // namespace

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
}

bool TranslucentWindowsSupported(QPoint globalPosition) {
	if (::Platform::IsWayland()) {
		return true;
	}
	if (const auto native = QGuiApplication::platformNativeInterface()) {
		if (const auto desktop = QApplication::desktop()) {
			if (const auto screen = base::QScreenNearestTo(globalPosition)) {
				if (native->nativeResourceForScreen(QByteArray("compositingEnabled"), screen)) {
					return true;
				}
				const auto index = QGuiApplication::screens().indexOf(screen);
				static auto WarnedAbout = base::flat_set<int>();
				if (!WarnedAbout.contains(index)) {
					WarnedAbout.emplace(index);
					UI_LOG(("WARNING: Compositing is disabled for screen index %1 (for position %2,%3)").arg(index).arg(globalPosition.x()).arg(globalPosition.y()));
				}
			} else {
				UI_LOG(("WARNING: Could not get screen for position %1,%2").arg(globalPosition.x()).arg(globalPosition.y()));
			}
		}
	}
	return false;
}

void IgnoreAllActivation(not_null<QWidget*> widget) {
}

bool WindowExtentsSupported() {
#ifdef DESKTOP_APP_QT_PATCHED
	if (::Platform::IsWayland()) {
		return true;
	}
#endif // DESKTOP_APP_QT_PATCHED

	namespace XCB = base::Platform::XCB;
	if (!::Platform::IsWayland()
		&& XCB::IsSupportedByWM(kXCBFrameExtentsAtomName.utf16())) {
		return true;
	}

	return false;
}

bool SetWindowExtents(QWindow *window, const QMargins &extents) {
	if (::Platform::IsWayland()) {
#ifdef DESKTOP_APP_QT_PATCHED
		window->setProperty("WaylandCustomMargins", QVariant::fromValue<QMargins>(extents));
		return true;
#else // DESKTOP_APP_QT_PATCHED
		return false;
#endif // !DESKTOP_APP_QT_PATCHED
	} else {
		return SetXCBFrameExtents(window, extents);
	}
}

bool UnsetWindowExtents(QWindow *window) {
	if (::Platform::IsWayland()) {
#ifdef DESKTOP_APP_QT_PATCHED
		window->setProperty("WaylandCustomMargins", QVariant());
		return true;
#else // DESKTOP_APP_QT_PATCHED
		return false;
#endif // !DESKTOP_APP_QT_PATCHED
	} else {
		return UnsetXCBFrameExtents(window);
	}
}

bool ShowWindowMenu(QWindow *window) {
	if (const auto integration = WaylandIntegration::Instance()) {
		return integration->showWindowMenu(window);
	} else {
		return ShowXCBWindowMenu(window);
	}
}

TitleControls::Layout TitleControlsLayout() {
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (const auto portalLayout = PortalTitleControlsLayout()) {
		return *portalLayout;
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	return TitleControls::Layout{
		.right = {
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
			TitleControls::Control::Close,
		}
	};
}

} // namespace Platform
} // namespace Ui
