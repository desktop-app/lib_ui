// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/linux/ui_utility_linux.h"

#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_library.h"
#include "base/platform/linux/base_linux_xcb_utilities.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <QtCore/QPoint>
#include <QtCore/QObject>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <qpa/qplatformwindow.h>
#include <qpa/qplatformwindow_p.h>

extern "C" {

typedef int32_t wl_fixed_t;
struct wl_object;
struct wl_array;
struct wl_proxy;
struct wl_display;
struct wl_registry;
struct wl_surface;
struct wl_message;
struct wl_interface;
struct xdg_toplevel;
struct zxdg_importer_v2;
struct zxdg_imported_v2;

union wl_argument {
	int32_t i;           /**< `int`    */
	uint32_t u;          /**< `uint`   */
	wl_fixed_t f;        /**< `fixed`  */
	const char *s;       /**< `string` */
	struct wl_object *o; /**< `object` */
	uint32_t n;          /**< `new_id` */
	struct wl_array *a;  /**< `array`  */
	int32_t h;           /**< `fd`     */
};

struct wl_message {
	const char *name;
	const char *signature;
	const struct wl_interface **types;
};

struct wl_interface {
	const char *name;
	int version;
	int method_count;
	const struct wl_message *methods;
	int event_count;
	const struct wl_message *events;
};

struct wl_registry_listener {
	void (*global)(
		void *data,
		struct wl_registry *registry,
		uint32_t name,
		const char *interface,
		uint32_t version);
	void (*global_remove)(
		void *data,
		struct wl_registry *registry,
		uint32_t name);
};

}

namespace Ui {
namespace Platform {
namespace {

static const auto kXCBFrameExtentsAtomName = u"_GTK_FRAME_EXTENTS"_q;

const auto kX11ForeignParentObjectName = u"_td_x11_foreign_parent"_q;

[[nodiscard]] QWindow *X11ForeignParentWindow(not_null<QWindow*> window) {
	return window->findChild<QWindow*>(
		kX11ForeignParentObjectName,
		Qt::FindDirectChildrenOnly);
}

using namespace base::Platform::XCB::Library;

void SetXCBFrameExtents(not_null<QWidget*> widget, const QMargins &extents) {
	static const auto xcb_delete_property_checked = LoadSymbol<
		xcb_void_cookie_t(xcb_connection_t*, xcb_window_t, xcb_atom_t)>(
		"xcb_delete_property_checked");

	const base::Platform::XCB::Connection connection;
	if (!connection || xcb_connection_has_error(connection)) {
		return;
	}

	const auto frameExtentsAtom = base::Platform::XCB::GetAtom(
		connection,
		kXCBFrameExtentsAtomName);

	if (!frameExtentsAtom) {
		return;
	}

	if (extents.isNull()) {
		free(
			xcb_request_check(
				connection,
				xcb_delete_property_checked(
					connection,
					widget->winId(),
					frameExtentsAtom)));
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

	free(
		xcb_request_check(
			connection,
			xcb_change_property_checked(
				connection,
				XCB_PROP_MODE_REPLACE,
				widget->winId(),
				frameExtentsAtom,
				XCB_ATOM_CARDINAL,
				32,
				extentsVector.size(),
				extentsVector.data())));
}

void ShowXCBWindowMenu(not_null<QWidget*> widget, const QPoint &point) {
	static const auto xcb_ungrab_pointer_checked = LoadSymbol<
		xcb_void_cookie_t(xcb_connection_t*, xcb_timestamp_t)>(
		"xcb_ungrab_pointer_checked");

	const base::Platform::XCB::Connection connection;
	if (!connection || xcb_connection_has_error(connection)) {
		return;
	}

	const auto root = base::Platform::XCB::GetRootWindow(connection);
	if (!root) {
		return;
	}

	const auto showWindowMenuAtom = base::Platform::XCB::GetAtom(
		connection,
		"_GTK_SHOW_WINDOW_MENU");

	if (!showWindowMenuAtom) {
		return;
	}

	const auto globalPos = point
		* widget->windowHandle()->devicePixelRatio()
		+ widget->windowHandle()->handle()->geometry().topLeft();

	xcb_client_message_event_t xev;
	xev.response_type = XCB_CLIENT_MESSAGE;
	xev.type = showWindowMenuAtom;
	xev.sequence = 0;
	xev.window = widget->winId();
	xev.format = 32;
	xev.data.data32[0] = 0;
	xev.data.data32[1] = globalPos.x();
	xev.data.data32[2] = globalPos.y();
	xev.data.data32[3] = 0;
	xev.data.data32[4] = 0;

	free(
		xcb_request_check(
			connection,
			xcb_ungrab_pointer_checked(connection, XCB_CURRENT_TIME)));

	free(
		xcb_request_check(
			connection,
			xcb_send_event_checked(
				connection,
				false,
				root,
				XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
					| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
				reinterpret_cast<const char*>(&xev))));
}

#if defined QT_FEATURE_wayland && QT_CONFIG(wayland)
constexpr auto kWaylandMarshalFlagDestroy = uint32_t(1);
const auto kWaylandImportedObjectName = u"_td_wayland_foreign_imported"_q;

const auto kWaylandXdgImporterRequests = std::array{
	wl_message{ "destroy", "", nullptr },
	wl_message{ "import_toplevel", "ns", nullptr },
};
const auto kWaylandXdgImportedRequests = std::array{
	wl_message{ "destroy", "", nullptr },
	wl_message{ "set_parent_of", "o", nullptr },
};
const auto kWaylandXdgImportedEvents = std::array{
	wl_message{ "destroyed", "", nullptr },
};
const auto kWaylandXdgImporterInterface = wl_interface{
	"zxdg_importer_v2",
	1,
	int(kWaylandXdgImporterRequests.size()),
	kWaylandXdgImporterRequests.data(),
	0,
	nullptr,
};
const auto kWaylandXdgImportedInterface = wl_interface{
	"zxdg_imported_v2",
	1,
	int(kWaylandXdgImportedRequests.size()),
	kWaylandXdgImportedRequests.data(),
	int(kWaylandXdgImportedEvents.size()),
	kWaylandXdgImportedEvents.data(),
};

struct WaylandSymbols {
	struct wl_proxy *(*proxyMarshalFlags)(
		struct wl_proxy *proxy,
		uint32_t opcode,
		const struct wl_interface *interface,
		uint32_t version,
		uint32_t flags,
		...) = nullptr;
	int (*proxyAddListener)(
		struct wl_proxy *proxy,
		void (**implementation)(void),
		void *data) = nullptr;
	void (*proxyDestroy)(struct wl_proxy *proxy) = nullptr;
	uint32_t (*proxyGetVersion)(struct wl_proxy *proxy) = nullptr;
	int (*displayRoundtrip)(struct wl_display *display) = nullptr;
	const struct wl_interface *registryInterface = nullptr;

	[[nodiscard]] explicit operator bool() const {
		return proxyMarshalFlags
			&& proxyAddListener
			&& proxyDestroy
			&& proxyGetVersion
			&& displayRoundtrip
			&& registryInterface;
	}
};

[[nodiscard]] const WaylandSymbols *Wayland() {
	static const auto result = [] {
		auto result = WaylandSymbols();
		if (const auto lib = base::Platform::LoadLibrary(
				"libwayland-client.so.0",
				RTLD_NODELETE)) {
			base::Platform::LoadSymbol(
				lib,
				"wl_proxy_marshal_flags",
				result.proxyMarshalFlags);
			base::Platform::LoadSymbol(
				lib,
				"wl_proxy_add_listener",
				result.proxyAddListener);
			base::Platform::LoadSymbol(
				lib,
				"wl_proxy_destroy",
				result.proxyDestroy);
			base::Platform::LoadSymbol(
				lib,
				"wl_proxy_get_version",
				result.proxyGetVersion);
			base::Platform::LoadSymbol(
				lib,
				"wl_display_roundtrip",
				result.displayRoundtrip);
			base::Platform::LoadSymbol(
				lib,
				"wl_registry_interface",
				result.registryInterface);
		}
		return result;
	}();

	return result ? &result : nullptr;
}

void DestroyWaylandProxy(const WaylandSymbols &wayland, struct wl_proxy *proxy) {
	if (proxy) {
		wayland.proxyMarshalFlags(
			proxy,
			0,
			nullptr,
			wayland.proxyGetVersion(proxy),
			kWaylandMarshalFlagDestroy);
	}
}

class WaylandImportedParent final : public QObject {
public:
	WaylandImportedParent(not_null<QWindow*> window, QString handle);
	~WaylandImportedParent() override;

	[[nodiscard]] const QString &handle() const {
		return _handle;
	}
	void refresh() {
		apply();
	}

	static void ImportedDestroyed(
		void *data,
		struct zxdg_imported_v2 *imported);

private:
	void apply();
	void destroyImported();

	const not_null<QWindow*> _window;
	const QString _handle;
	struct zxdg_imported_v2 *_imported = nullptr;

};

struct WaylandImportedListener {
	void (*destroyed)(void *data, struct zxdg_imported_v2 *imported);
};
auto kWaylandImportedListener = WaylandImportedListener{
	&WaylandImportedParent::ImportedDestroyed,
};

struct WaylandRegistryResult {
	uint32_t name = 0;
	uint32_t version = 0;
};

void WaylandRegistryGlobal(
		void *data,
		struct wl_registry *,
		uint32_t name,
		const char *interface,
		uint32_t version) {
	if (!interface || std::strcmp(interface, kWaylandXdgImporterInterface.name)) {
		return;
	}
	const auto result = static_cast<WaylandRegistryResult*>(data);
	result->name = name;
	result->version = std::min<uint32_t>(
		version,
		kWaylandXdgImporterInterface.version);
}

void WaylandRegistryGlobalRemove(
		void *,
		struct wl_registry *,
		uint32_t) {
}

auto kWaylandRegistryListener = wl_registry_listener{
	&WaylandRegistryGlobal,
	&WaylandRegistryGlobalRemove,
};

[[nodiscard]] struct zxdg_importer_v2 *WaylandImporter(
		struct wl_display *display) {
	struct State {
		struct wl_display *display = nullptr;
		struct zxdg_importer_v2 *importer = nullptr;
		bool resolved = false;
	};
	static auto state = State();
	const auto wayland = Wayland();
	if (!wayland || !display) {
		return nullptr;
	}
	if (state.display != display) {
		if (state.importer) {
			DestroyWaylandProxy(
				*wayland,
				reinterpret_cast<wl_proxy*>(state.importer));
		}
		state = {
			.display = display,
		};
	}
	if (state.resolved) {
		return state.importer;
	}
	state.resolved = true;

	const auto displayProxy = reinterpret_cast<wl_proxy*>(display);
	const auto registry = reinterpret_cast<wl_registry*>(
		wayland->proxyMarshalFlags(
			displayProxy,
			1,
			wayland->registryInterface,
			wayland->proxyGetVersion(displayProxy),
			0,
			nullptr));
	if (!registry) {
		return nullptr;
	}

	auto result = WaylandRegistryResult();
	wayland->proxyAddListener(
		reinterpret_cast<wl_proxy*>(registry),
		reinterpret_cast<void(**)(void)>(&kWaylandRegistryListener),
		&result);
	wayland->displayRoundtrip(display);
	if (result.name && result.version) {
		state.importer = reinterpret_cast<zxdg_importer_v2*>(
			wayland->proxyMarshalFlags(
				reinterpret_cast<wl_proxy*>(registry),
				0,
				&kWaylandXdgImporterInterface,
				result.version,
				0,
				result.name,
				kWaylandXdgImporterInterface.name,
				result.version,
				nullptr));
	}
	wayland->proxyDestroy(reinterpret_cast<wl_proxy*>(registry));
	return state.importer;
}

WaylandImportedParent::WaylandImportedParent(
	not_null<QWindow*> window,
	QString handle)
: QObject(window.get())
, _window(window)
, _handle(std::move(handle)) {
	setObjectName(kWaylandImportedObjectName);
	apply();
}

WaylandImportedParent::~WaylandImportedParent() {
	destroyImported();
}

void WaylandImportedParent::apply() {
	if (_imported || _handle.isEmpty()) {
		return;
	}

	using namespace QNativeInterface;
	using namespace QNativeInterface::Private;
	const auto native = qApp->nativeInterface<QWaylandApplication>();
	const auto nativeWindow = _window->nativeInterface<QWaylandWindow>();
	if (!native || !nativeWindow) {
		return;
	}

	const auto surface = nativeWindow->surface();
	if (!surface) {
		QObject::connect(
			nativeWindow,
			&QWaylandWindow::surfaceCreated,
			this,
			[this] { apply(); },
			Qt::SingleShotConnection);
		return;
	}
	if (!nativeWindow->surfaceRole<xdg_toplevel>()) {
		QObject::connect(
			nativeWindow,
			&QWaylandWindow::surfaceRoleCreated,
			this,
			[this] { apply(); },
			Qt::SingleShotConnection);
		return;
	}

	const auto wayland = Wayland();
	const auto importer = WaylandImporter(native->display());
	if (!wayland || !importer) {
		return;
	}

	const auto handle = _handle.toUtf8();
	_imported = reinterpret_cast<zxdg_imported_v2*>(
		wayland->proxyMarshalFlags(
			reinterpret_cast<wl_proxy*>(importer),
			1,
			&kWaylandXdgImportedInterface,
			kWaylandXdgImportedInterface.version,
			0,
			nullptr,
			handle.constData()));
	if (!_imported) {
		return;
	}

	wayland->proxyAddListener(
		reinterpret_cast<wl_proxy*>(_imported),
		reinterpret_cast<void(**)(void)>(&kWaylandImportedListener),
		this);
	wayland->proxyMarshalFlags(
		reinterpret_cast<wl_proxy*>(_imported),
		1,
		nullptr,
		wayland->proxyGetVersion(reinterpret_cast<wl_proxy*>(_imported)),
		0,
		surface);
}

void WaylandImportedParent::destroyImported() {
	const auto imported = _imported;
	_imported = nullptr;
	if (!imported) {
		return;
	}
	if (const auto wayland = Wayland()) {
		DestroyWaylandProxy(*wayland, reinterpret_cast<wl_proxy*>(imported));
	}
}

void WaylandImportedParent::ImportedDestroyed(
		void *data,
		struct zxdg_imported_v2 *) {
	const auto self = static_cast<WaylandImportedParent*>(data);
	self->_imported = nullptr;
}

void ShowWaylandWindowMenu(not_null<QWidget*> widget, const QPoint &point) {
	static const auto wl_proxy_marshal_array = [] {
		void (*result)(
			struct wl_proxy *p,
			uint32_t opcode,
			union wl_argument *args) = nullptr;

		if (const auto lib = base::Platform::LoadLibrary(
				"libwayland-client.so.0",
				RTLD_NODELETE)) {
			base::Platform::LoadSymbol(lib, "wl_proxy_marshal_array", result);
		}

		return result;
	}();

	if (!wl_proxy_marshal_array) {
		return;
	}

	using namespace QNativeInterface;
	using namespace QNativeInterface::Private;
	const auto window = not_null(widget->windowHandle());
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

	const auto pos = point
		* window->devicePixelRatio()
		/ window->handle()->devicePixelRatio();

	wl_proxy_marshal_array(
		reinterpret_cast<wl_proxy*>(toplevel),
		4, // XDG_TOPLEVEL_SHOW_WINDOW_MENU
		std::array{
			wl_argument{ .o = reinterpret_cast<wl_object*>(seat) },
			wl_argument{ .u = native->lastInputSerial() },
			wl_argument{ .i = pos.x() },
			wl_argument{ .i = pos.y() },
		}.data());
}
#endif // wayland

} // namespace

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
}

bool TranslucentWindowsSupported() {
	if (::Platform::IsWayland()) {
		return true;
	}

	if (::Platform::IsX11()) {
		const base::Platform::XCB::Connection connection;
		if (!connection || xcb_connection_has_error(connection)) {
			return false;
		}

		const auto atom = base::Platform::XCB::GetAtom(
			connection,
			"_NET_WM_CM_S0");

		if (!atom) {
			return false;
		}

		const auto cookie = xcb_get_selection_owner(connection, atom);

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

	return false;
}

void IgnoreAllActivation(not_null<QWidget*> widget) {
}

void ClearTransientParent(not_null<QWidget*> widget) {
	if (::Platform::IsX11()) {
		static const auto xcb_delete_property_checked = LoadSymbol<
			xcb_void_cookie_t(xcb_connection_t*, xcb_window_t, xcb_atom_t)>(
			"xcb_delete_property_checked");

		const base::Platform::XCB::Connection connection;
		if (!connection || xcb_connection_has_error(connection)) {
			return;
		}

		free(
			xcb_request_check(
				connection,
				xcb_delete_property_checked(
					connection,
					widget->winId(),
					XCB_ATOM_WM_TRANSIENT_FOR)));

		return;
	}
}

void SetForeignTransientParent(
		not_null<QWidget*> widget,
		const ForeignParent &parent) {
#if defined QT_FEATURE_wayland && QT_CONFIG(wayland)
	if (::Platform::IsWayland()) {
		const auto window = widget->windowHandle();
		if (!window) {
			return;
		}
		if (const auto existingObject = window->findChild<QObject*>(
				kWaylandImportedObjectName,
				Qt::FindDirectChildrenOnly)) {
			const auto existing = static_cast<WaylandImportedParent*>(
				existingObject);
			if ((parent.type != ForeignParent::Type::Wayland)
				|| parent.wayland.isEmpty()
				|| (existing->handle() != parent.wayland)) {
				delete existing;
			} else {
				existing->refresh();
				return;
			}
		}
		if ((parent.type == ForeignParent::Type::Wayland)
			&& !parent.wayland.isEmpty()) {
			new WaylandImportedParent(window, parent.wayland);
		}
		return;
	}
#endif // wayland

	if (::Platform::IsX11()) {
		const auto window = widget->windowHandle();
		if (!window) {
			return;
		}
		const auto id = (parent.type == ForeignParent::Type::X11)
			? WId(parent.x11)
			: WId(0);
		if (const auto existing = X11ForeignParentWindow(window)) {
			if (id && (existing->winId() == id)) {
				window->setTransientParent(existing);
				return;
			}
			if (window->transientParent() == existing) {
				window->setTransientParent(nullptr);
			}
			delete existing;
		}
		if (!id) {
			return;
		}

		// Let Qt know about the transient parent, so that it writes
		// the right WM_TRANSIENT_FOR itself: QXcbWindow::show() rewrites
		// the property on each show (to the client leader by default),
		// clobbering any value we could set by hand.
		const auto foreign = QWindow::fromWinId(id);
		if (!foreign) {
			return;
		}
		foreign->setObjectName(kX11ForeignParentObjectName);
		static_cast<QObject*>(foreign)->setParent(window);
		window->setTransientParent(foreign);
	}
}

bool WindowMarginsSupported() {
#if defined QT_FEATURE_wayland && QT_CONFIG(wayland)
	static const auto WaylandResult = [] {
		using namespace QNativeInterface::Private;
		QWindow window;
		window.create();
		return bool(window.nativeInterface<QWaylandWindow>());
	}();

	if (WaylandResult) {
		return true;
	}
#endif // wayland

	namespace XCB = base::Platform::XCB;
	if (::Platform::IsX11()
		&& XCB::IsSupportedByWM(
			XCB::Connection(),
			kXCBFrameExtentsAtomName)) {
		return true;
	}

	return false;
}

void SetWindowMargins(not_null<QWidget*> widget, const QMargins &margins) {
#if defined QT_FEATURE_wayland && QT_CONFIG(wayland)
	using namespace QNativeInterface::Private;
	const auto window = not_null(widget->windowHandle());
	const auto platformWindow = not_null(window->handle());
	if (const auto native = window->nativeInterface<QWaylandWindow>()) {
		native->setCustomMargins(
			margins
				* window->devicePixelRatio()
				/ platformWindow->devicePixelRatio());
		return;
	}
#endif // wayland

	if (::Platform::IsX11()) {
		SetXCBFrameExtents(widget, margins);
		return;
	}
}

void ShowWindowMenu(not_null<QWidget*> widget, const QPoint &point) {
#if defined QT_FEATURE_wayland && QT_CONFIG(wayland)
	if (::Platform::IsWayland()) {
		ShowWaylandWindowMenu(widget, point);
		return;
	}
#endif // wayland

	if (::Platform::IsX11()) {
		ShowXCBWindowMenu(widget, point);
		return;
	}
}

} // namespace Platform
} // namespace Ui
