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

#include <QtCore/QPointer>
#include <QtCore/QCoreApplication>

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
	crl::on_main(Window, [=] {
		if (const auto widget = base::take(Window)) {
			if (const auto window = widget->window()) {
				if (!window->isHidden()) {
					window->raise();
					window->activateWindow();
				}
			}
		}
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
