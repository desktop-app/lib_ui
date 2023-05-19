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

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <QtCore/QPointer>
#include <QtWidgets/QApplication>

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

void ActivateWindow(not_null<QWidget*> widget) {
	const auto window = widget->window();
	window->raise();
	window->activateWindow();
	ActivateWindowDelayed(window);
}

void ActivateWindowDelayed(not_null<QWidget*> widget) {
	if (Paused) {
		Attempted = true;
		return;
	} else if (std::exchange(Window, widget.get())) {
		return;
	}
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	const auto focusAncestor = [&] {
		const auto focusWidget = QApplication::focusWidget();
		if (!focusWidget || !widget->window()) {
			return false;
		}
		return widget->window()->isAncestorOf(focusWidget);
	}();
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
	crl::on_main(Window, [=] {
		const auto widget = base::take(Window);
		if (!widget) {
			return;
		}
		const auto window = widget->window();
		if (!window || window->isHidden()) {
			return;
		}
		window->raise();
		window->activateWindow();
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
		if (::Platform::IsX11() && focusAncestor) {
			xcb_set_input_focus(
				base::Platform::XCB::GetConnectionFromQt(),
				XCB_INPUT_FOCUS_PARENT,
				window->winId(),
				XCB_CURRENT_TIME);
		}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
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
