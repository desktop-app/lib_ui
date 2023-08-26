// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/linux/ui_utility_linux.h"

#include "base/platform/base_platform_info.h"
#include "base/call_delayed.h"
#include "ui/platform/linux/ui_linux_wayland_integration.h"

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <QtCore/QPoint>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <qpa/qplatformwindow_p.h>

namespace Ui {
namespace Platform {
namespace {

static const auto kXCBFrameExtentsAtomName = u"_GTK_FRAME_EXTENTS"_q;
constexpr auto kDelayDeactivateEventTimeout = crl::time(400);

bool PendingDeactivateEvent/* = false*/;
int ChildPopupsHiddenOnWayland/* = 0*/;

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
std::optional<bool> XCBWindowMapped(xcb_window_t window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return std::nullopt;
	}

	const auto cookie = xcb_get_window_attributes(connection, window);
	const auto reply = base::Platform::XCB::MakeReplyPointer(
		xcb_get_window_attributes_reply(
			connection,
			cookie,
			nullptr));

	if (!reply) {
		return std::nullopt;
	}

	return reply->map_state == XCB_MAP_STATE_VIEWABLE;
}

std::optional<bool> XCBWindowHidden(xcb_window_t window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return std::nullopt;
	}

	const auto stateAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE");

	const auto stateHiddenAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE_HIDDEN");

	if (!stateAtom.has_value() || !stateHiddenAtom.has_value()) {
		return std::nullopt;
	}

	const auto cookie = xcb_get_property(
		connection,
		false,
		window,
		*stateAtom,
		XCB_ATOM_ATOM,
		0,
		1024);

	const auto reply = base::Platform::XCB::MakeReplyPointer(
		xcb_get_property_reply(
			connection,
			cookie,
			nullptr));

	if (!reply) {
		return std::nullopt;
	}

	if (reply->type != XCB_ATOM_ATOM || reply->format != 32) {
		return std::nullopt;
	}

	const auto atomsStart = reinterpret_cast<xcb_atom_t*>(
		xcb_get_property_value(reply.get()));

	const auto states = std::vector<xcb_atom_t>(
		atomsStart,
		atomsStart + reply->length);

	return ranges::contains(states, *stateHiddenAtom);
}

QRect XCBWindowGeometry(xcb_window_t window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return {};
	}

	const auto cookie = xcb_get_geometry(connection, window);
	const auto reply = base::Platform::XCB::MakeReplyPointer(
		xcb_get_geometry_reply(
			connection,
			cookie,
			nullptr));

	if (!reply) {
		return {};
	}

	return QRect(reply->x, reply->y, reply->width, reply->height);
}

std::optional<uint> XCBCurrentWorkspace() {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return std::nullopt;
	}

	const auto root = base::Platform::XCB::GetRootWindow(connection);
	if (!root.has_value()) {
		return std::nullopt;
	}

	const auto currentDesktopAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_CURRENT_DESKTOP");

	if (!currentDesktopAtom.has_value()) {
		return std::nullopt;
	}

	const auto cookie = xcb_get_property(
		connection,
		false,
		*root,
		*currentDesktopAtom,
		XCB_ATOM_CARDINAL,
		0,
		1024);

	const auto reply = base::Platform::XCB::MakeReplyPointer(
		xcb_get_property_reply(
			connection,
			cookie,
			nullptr));

	if (!reply) {
		return std::nullopt;
	}

	return (reply->type == XCB_ATOM_CARDINAL)
		? std::make_optional(
			*reinterpret_cast<ulong*>(xcb_get_property_value(reply.get())))
		: std::nullopt;
}

std::optional<uint> XCBWindowWorkspace(xcb_window_t window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return std::nullopt;
	}

	const auto desktopAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_DESKTOP");

	if (!desktopAtom.has_value()) {
		return std::nullopt;
	}

	const auto cookie = xcb_get_property(
		connection,
		false,
		window,
		*desktopAtom,
		XCB_ATOM_CARDINAL,
		0,
		1024);

	const auto reply = base::Platform::XCB::MakeReplyPointer(
		xcb_get_property_reply(
			connection,
			cookie,
			nullptr));

	if (!reply) {
		return std::nullopt;
	}

	return (reply->type == XCB_ATOM_CARDINAL)
		? std::make_optional(
			*reinterpret_cast<ulong*>(xcb_get_property_value(reply.get())))
		: std::nullopt;
}

std::optional<bool> XCBIsOverlapped(
		not_null<QWidget*> widget,
		const QRect &rect) {
	const auto window = widget->winId();
	Expects(window != XCB_WINDOW_NONE);

	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return std::nullopt;
	}

	const auto root = base::Platform::XCB::GetRootWindow(connection);
	if (!root.has_value()) {
		return std::nullopt;
	}

	const auto windowWorkspace = XCBWindowWorkspace(window);
	const auto currentWorkspace = XCBCurrentWorkspace();
	if (windowWorkspace.has_value()
		&& currentWorkspace.has_value()
		&& *windowWorkspace != *currentWorkspace
		&& *windowWorkspace != 0xFFFFFFFF) {
		return true;
	}

	const auto windowGeometry = XCBWindowGeometry(window);
	if (windowGeometry.isNull()) {
		return std::nullopt;
	}

	const auto mappedRect = QRect(
		rect.topLeft()
			* widget->windowHandle()->devicePixelRatio()
			+ windowGeometry.topLeft(),
		rect.size() * widget->windowHandle()->devicePixelRatio());

	const auto cookie = xcb_query_tree(connection, *root);
	const auto reply = base::Platform::XCB::MakeReplyPointer(
		xcb_query_tree_reply(connection, cookie, nullptr));

	if (!reply) {
		return std::nullopt;
	}

	const auto tree = xcb_query_tree_children(reply.get());
	auto aboveTheWindow = false;

	for (auto i = 0, l = xcb_query_tree_children_length(reply.get()); i < l; ++i) {
		if (window == tree[i]) {
			aboveTheWindow = true;
			continue;
		}

		if (!aboveTheWindow) {
			continue;
		}

		const auto geometry = XCBWindowGeometry(tree[i]);
		if (!mappedRect.intersects(geometry)) {
			continue;
		}

		const auto workspace = XCBWindowWorkspace(tree[i]);
		if (workspace.has_value()
			&& windowWorkspace.has_value()
			&& *workspace != *windowWorkspace
			&& *workspace != 0xFFFFFFFF) {
			continue;
		}

		const auto mapped = XCBWindowMapped(tree[i]);
		if (mapped.has_value() && !*mapped) {
			continue;
		}

		const auto hidden = XCBWindowHidden(tree[i]);
		if (hidden.has_value() && *hidden) {
			continue;
		}

		return true;
	}

	return false;
}

void SetXCBFrameExtents(not_null<QWidget*> widget, const QMargins &extents) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName);

	if (!frameExtentsAtom.has_value()) {
		return;
	}

	const auto nativeExtents = extents
		* widget->windowHandle()->devicePixelRatio();

	const auto extentsVector = std::vector<uint>{
		uint(nativeExtents.left()),
		uint(nativeExtents.right()),
		uint(nativeExtents.top()),
		uint(nativeExtents.bottom()),
	};

	xcb_change_property(
		connection,
		XCB_PROP_MODE_REPLACE,
		widget->winId(),
		*frameExtentsAtom,
		XCB_ATOM_CARDINAL,
		32,
		extentsVector.size(),
		extentsVector.data());
}

void UnsetXCBFrameExtents(not_null<QWidget*> widget) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName);

	if (!frameExtentsAtom.has_value()) {
		return;
	}

	xcb_delete_property(
		connection,
		widget->winId(),
		*frameExtentsAtom);
}

void ShowXCBWindowMenu(not_null<QWidget*> widget, const QPoint &point) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return;
	}

	const auto root = base::Platform::XCB::GetRootWindow(connection);
	if (!root.has_value()) {
		return;
	}

	const auto showWindowMenuAtom = base::Platform::XCB::GetAtom(
		connection,
		"_GTK_SHOW_WINDOW_MENU");

	if (!showWindowMenuAtom.has_value()) {
		return;
	}

	const auto windowGeometry = XCBWindowGeometry(widget->winId());
	if (windowGeometry.isNull()) {
		return;
	}

	const auto globalPos = point
		* widget->windowHandle()->devicePixelRatio()
		+ windowGeometry.topLeft();

	xcb_client_message_event_t xev;
	xev.response_type = XCB_CLIENT_MESSAGE;
	xev.type = *showWindowMenuAtom;
	xev.sequence = 0;
	xev.window = widget->winId();
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
}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

} // namespace

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
}

bool TranslucentWindowsSupported() {
	if (::Platform::IsWayland()) {
		return true;
	}

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (::Platform::IsX11()) {
		const auto connection = base::Platform::XCB::GetConnectionFromQt();
		if (!connection) {
			return false;
		}

		const auto atom = base::Platform::XCB::GetAtom(
			connection,
			"_NET_WM_CM_S0");

		if (!atom) {
			return false;
		}

		const auto cookie = xcb_get_selection_owner(connection, *atom);

		const auto result = base::Platform::XCB::MakeReplyPointer(
			xcb_get_selection_owner_reply(
				connection,
				cookie,
				nullptr));

		if (!result) {
			return false;
		}

		return result->owner;
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	return false;
}

void IgnoreAllActivation(not_null<QWidget*> widget) {
}

void ClearTransientParent(not_null<QWidget*> widget) {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (::Platform::IsX11()) {
		xcb_delete_property(
			base::Platform::XCB::GetConnectionFromQt(),
			widget->winId(),
			XCB_ATOM_WM_TRANSIENT_FOR);
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
}

std::optional<bool> IsOverlapped(
		not_null<QWidget*> widget,
		const QRect &rect) {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (::Platform::IsX11()) {
		return XCBIsOverlapped(widget, rect);
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	return std::nullopt;
}

bool WindowMarginsSupported() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	using namespace QNativeInterface::Private;
	QWindow window;
	window.create();
	if (window.nativeInterface<QWaylandWindow>()) {
		return true;
	}
#endif // Qt >= 6.5.0

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	namespace XCB = base::Platform::XCB;
	if (::Platform::IsX11()
		&& XCB::IsSupportedByWM(
			XCB::GetConnectionFromQt(),
			kXCBFrameExtentsAtomName)) {
		return true;
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	return false;
}

void SetWindowMargins(not_null<QWidget*> widget, const QMargins &margins) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	using namespace QNativeInterface::Private;
	if (const auto native = not_null(widget->windowHandle())
			->nativeInterface<QWaylandWindow>()) {
		native->setCustomMargins(margins);
		return;
	}
#endif // Qt >= 6.5.0

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (::Platform::IsX11()) {
		SetXCBFrameExtents(widget, margins);
		return;
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
}

void UnsetWindowMargins(not_null<QWidget*> widget) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	using namespace QNativeInterface::Private;
	if (const auto native = not_null(widget->windowHandle())
			->nativeInterface<QWaylandWindow>()) {
		native->setCustomMargins(QMargins());
		return;
	}
#endif // Qt >= 6.5.0

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (::Platform::IsX11()) {
		UnsetXCBFrameExtents(widget);
		return;
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
}

void ShowWindowMenu(not_null<QWidget*> widget, const QPoint &point) {
	if (const auto integration = WaylandIntegration::Instance()) {
		integration->showWindowMenu(widget, point);
	} else if (::Platform::IsX11()) {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
		ShowXCBWindowMenu(widget, point);
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
	}
}

void RegisterChildPopupHiding() {
	if (!::Platform::IsWayland()) {
		return;
	}
	++ChildPopupsHiddenOnWayland;
	base::call_delayed(kDelayDeactivateEventTimeout, [] {
		if (!--ChildPopupsHiddenOnWayland) {
			if (base::take(PendingDeactivateEvent)) {
				// We didn't receive ApplicationActivate event in time.
				QEvent appDeactivate(QEvent::ApplicationDeactivate);
				QCoreApplication::sendEvent(qApp, &appDeactivate);
			}
		}
	});
}

bool SkipApplicationDeactivateEvent() {
	if (!ChildPopupsHiddenOnWayland) {
		return false;
	}
	PendingDeactivateEvent = true;
	return true;
}

void GotApplicationActivateEvent() {
	PendingDeactivateEvent = false;
}

} // namespace Platform
} // namespace Ui
