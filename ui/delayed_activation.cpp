// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/delayed_activation.h"

#include "ui/ui_utility.h"

#include <QtCore/QPointer>

namespace Ui {
namespace {

auto Paused = false;
auto Window = QPointer<QWidget>();

} // namespace

void ActivateWindowDelayed(not_null<QWidget*> widget) {
	if (Paused) {
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
		Paused = false;
	});
}

} // namespace Ui
