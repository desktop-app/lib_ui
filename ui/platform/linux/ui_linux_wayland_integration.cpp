/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/linux/ui_linux_wayland_integration.h"

#include "base/platform/linux/base_linux_wayland_utilities.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow_p.h>
#include <qwayland-wayland.h>

using namespace QNativeInterface;
using namespace QNativeInterface::Private;
using namespace base::Platform::Wayland;

namespace Ui {
namespace Platform {

struct WaylandIntegration::Private : public AutoDestroyer<QtWayland::wl_registry> {
	std::optional<uint32_t> xdgDecoration;

protected:
	void registry_global(
			uint32_t name,
			const QString &interface,
			uint32_t version) override {
		if (interface == qstr("zxdg_decoration_manager_v1")) {
			xdgDecoration = name;
		}
	}

	void registry_global_remove(uint32_t name) override {
		if (xdgDecoration && name == *xdgDecoration) {
			xdgDecoration = std::nullopt;
		}
	}
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

	_private->init(wl_display_get_registry(display));
	wl_display_roundtrip(display);
}

WaylandIntegration::~WaylandIntegration() = default;

WaylandIntegration *WaylandIntegration::Instance() {
	if (!::Platform::IsWayland()) return nullptr;
	static std::optional<WaylandIntegration> instance(std::in_place);
	[[maybe_unused]] static const auto Inited = [] {
		base::qt_signal_producer(
			QGuiApplication::platformNativeInterface(),
			&QObject::destroyed
		) | rpl::start_with_next([] {
			instance = std::nullopt;
		}, instance->_private->lifetime());
		return true;
	}();
	if (!instance) return nullptr;
	return &*instance;
}

bool WaylandIntegration::xdgDecorationSupported() {
	return _private->xdgDecoration.has_value();
}

} // namespace Platform
} // namespace Ui
