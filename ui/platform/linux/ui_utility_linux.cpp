// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/linux/ui_utility_linux.h"

#include "ui/ui_log.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_gtk_integration.h"
#include "ui/platform/linux/ui_linux_wayland_integration.h"
#include "base/const_string.h"
#include "base/qt_adapters.h"
#include "base/flat_set.h"

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <QtCore/QPoint>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <qpa/qplatformnativeinterface.h>

Q_DECLARE_METATYPE(QMargins);

namespace Ui {
namespace Platform {
namespace {

constexpr auto kXCBFrameExtentsAtomName = "_GTK_FRAME_EXTENTS"_cs;

constexpr auto kXDGDesktopPortalService = "org.freedesktop.portal.Desktop"_cs;
constexpr auto kXDGDesktopPortalObjectPath = "/org/freedesktop/portal/desktop"_cs;
constexpr auto kSettingsPortalInterface = "org.freedesktop.portal.Settings"_cs;

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
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
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

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

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	namespace XCB = base::Platform::XCB;
	if (!::Platform::IsWayland()
		&& XCB::IsSupportedByWM(kXCBFrameExtentsAtomName.utf16())) {
		return true;
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

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
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
		return SetXCBFrameExtents(window, extents);
#else // !DESKTOP_APP_DISABLE_X11_INTEGRATION
		return false;
#endif // DESKTOP_APP_DISABLE_X11_INTEGRATION
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
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
		return UnsetXCBFrameExtents(window);
#else // !DESKTOP_APP_DISABLE_X11_INTEGRATION
		return false;
#endif // DESKTOP_APP_DISABLE_X11_INTEGRATION
	}
}

bool ShowWindowMenu(QWindow *window) {
	if (const auto integration = WaylandIntegration::Instance()) {
		return integration->showWindowMenu(window);
	} else {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
		return ShowXCBWindowMenu(window);
#else // !DESKTOP_APP_DISABLE_X11_INTEGRATION
		return false;
#endif // DESKTOP_APP_DISABLE_X11_INTEGRATION
	}
}

TitleControls::Layout TitleControlsLayout() {
	const auto gtkResult = []() -> std::optional<TitleControls::Layout> {
		const auto integration = base::Platform::GtkIntegration::Instance();
		if (!integration || !integration->checkVersion(3, 12, 0)) {
			return std::nullopt;
		}

		const auto decorationLayoutSetting = integration->getStringSetting(
			"gtk-decoration-layout");
		
		if (!decorationLayoutSetting.has_value()) {
			return std::nullopt;
		}

		const auto decorationLayout = decorationLayoutSetting->split(':');

		std::vector<TitleControls::Control> controlsLeft;
		ranges::transform(
			decorationLayout[0].split(','),
			ranges::back_inserter(controlsLeft),
			GtkKeywordToTitleControl);

		std::vector<TitleControls::Control> controlsRight;
		if (decorationLayout.size() > 1) {
			ranges::transform(
				decorationLayout[1].split(','),
				ranges::back_inserter(controlsRight),
				GtkKeywordToTitleControl);
		}

		return TitleControls::Layout{
			.left = controlsLeft,
			.right = controlsRight
		};
	}();

	if (gtkResult.has_value()) {
		return *gtkResult;
	}

#ifdef __HAIKU__
	return TitleControls::Layout{
		.left = {
			TitleControls::Control::Close,
		},
		.right = {
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
		}
	};
#else // __HAIKU__
	return TitleControls::Layout{
		.right = {
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
			TitleControls::Control::Close,
		}
	};
#endif // !__HAIKU__
}

} // namespace Platform
} // namespace Ui
