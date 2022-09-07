// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/delayed_activation.h"

#include "ui/ui_utility.h"
#include "base/call_delayed.h"
#include "base/invoke_queued.h"
#include "base/platform/base_platform_info.h"

#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

namespace Ui {
namespace {

constexpr auto kPreventTimeout = crl::time(100);

bool Paused/* = false*/;
bool Attempted/* = false*/;
auto Window = QPointer<QWidget>();

bool Unpause(bool force = false) {
	if (force || Attempted) {
		Attempted = false;
		Paused = false;
		return true;
	}
	return false;
}

} // namespace

void ActivateWindowDelayed(not_null<QWidget*> widget) {
	if (Paused) {
		Attempted = true;
		return;
	} else if (std::exchange(Window, widget.get())) {
		return;
	}
	const auto focusAncestor = [&] {
		const auto focusWindow = QGuiApplication::focusWindow();
		if (!focusWindow || !widget->window()) {
			return false;
		}
		const auto handle = widget->window()->windowHandle();
		if (!handle) {
			return false;
		}
		return handle->isAncestorOf(focusWindow);
	}();
	crl::on_main(Window, [=] {
		const auto widget = base::take(Window);
		if (!widget) {
			return;
		}
		const auto window = widget->window();
		if (!window || window->isHidden()) {
			return;
		}
		const auto guard = [&] {
			if (!::Platform::IsX11() || !focusAncestor) {
				return gsl::finally(Fn<void()>([] {}));
			}
			const auto handle = window->windowHandle();
			if (handle->flags() & Qt::X11BypassWindowManagerHint) {
				return gsl::finally(Fn<void()>([] {}));
			}
			handle->setFlag(Qt::X11BypassWindowManagerHint);
			return gsl::finally(Fn<void()>([handle] {
				handle->setFlag(Qt::X11BypassWindowManagerHint, false);
			}));
		}();
		window->raise();
		window->activateWindow();
	});
}

void PreventDelayedActivation() {
	Window = nullptr;
	Paused = true;
	PostponeCall([] {
		if (Unpause()) {
			return;
		}
		InvokeQueued(qApp, [] {
			if (Unpause()) {
				return;
			}
			crl::on_main([] {
				if (Unpause()) {
					return;
				}
				base::call_delayed(kPreventTimeout, [] {
					Unpause(true);
				});
			});
		});
	});
}

} // namespace Ui
