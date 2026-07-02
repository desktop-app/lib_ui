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

#if !defined Q_OS_WIN && !defined Q_OS_MAC
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !Q_OS_WIN && !Q_OS_MAC

#include <QtCore/QPointer>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kPreventTimeout = crl::time(100);

bool Paused/* = false*/;
bool Attempted/* = false*/;
int KeepingPaused/* = 0*/;
auto Window = QPointer<QWidget>();

bool Unpause(bool force = false) {
	if ((force && !KeepingPaused) || Attempted) {
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
#if !defined Q_OS_WIN && !defined Q_OS_MAC
	const auto focusAncestor = [&] {
		const auto focusWidget = QApplication::focusWidget();
		if (!focusWidget || !widget->window()) {
			return false;
		}
		return widget->window()->isAncestorOf(focusWidget);
	}();
#endif // !Q_OS_WIN && !Q_OS_MAC
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
#if !defined Q_OS_WIN && !defined Q_OS_MAC
		if (::Platform::IsX11() && focusAncestor) {
			using namespace base::Platform::XCB::Library;
			static const auto xcb_set_input_focus_checked = LoadSymbol<
				xcb_void_cookie_t(
						xcb_connection_t*,
						uint8_t,
						xcb_window_t,
						xcb_timestamp_t)>("xcb_set_input_focus_checked");
			const base::Platform::XCB::Connection connection;
			if (connection && !xcb_connection_has_error(connection)) {
				free(
					xcb_request_check(
						connection,
						xcb_set_input_focus_checked(
							connection,
							XCB_INPUT_FOCUS_PARENT,
							window->winId(),
							XCB_CURRENT_TIME)));
			}
		}
#endif // !Q_OS_WIN && !Q_OS_MAC
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

void KeepDelayedActivationPaused(bool keep) {
	if (keep) {
		++KeepingPaused;
	} else if (KeepingPaused > 0) {
		if (!--KeepingPaused && Paused) {
			Unpause(true);
		}
	}
}

} // namespace Ui
