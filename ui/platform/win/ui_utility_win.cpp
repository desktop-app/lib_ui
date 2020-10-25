// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_utility_win.h"

#include <QtWidgets/QApplication>

#include "base/platform/win/base_windows_h.h"

namespace Ui {
namespace Platform {

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
}

void UpdateOverlayed(not_null<QWidget*> widget) {
	const auto wm = widget->testAttribute(Qt::WA_Mapped);
	const auto wv = widget->testAttribute(Qt::WA_WState_Visible);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, true);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, true);
	widget->update();
	QEvent e(QEvent::UpdateRequest);
	QGuiApplication::sendEvent(widget, &e);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, false);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, false);
}

void IgnoreAllActivation(not_null<QWidget*> widget) {
	widget->createWinId();

	const auto handle = reinterpret_cast<HWND>(widget->winId());
	Assert(handle != nullptr);

	ShowWindow(handle, SW_HIDE);
	const auto style = GetWindowLongPtr(handle, GWL_EXSTYLE);
	SetWindowLongPtr(
		handle,
		GWL_EXSTYLE,
		style | WS_EX_NOACTIVATE | WS_EX_APPWINDOW);
	ShowWindow(handle, SW_SHOW);
}

} // namespace Platform
} // namespace Ui
