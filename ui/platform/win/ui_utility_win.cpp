// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_utility_win.h"

#include "base/platform/win/base_windows_h.h"

#include <QtWidgets/QApplication>
#include <QtGui/QWindow>

#include <wrl/client.h>
#include <Shobjidl.h>

using namespace Microsoft::WRL;

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

std::optional<bool> IsOverlapped(
		not_null<QWidget*> widget,
		const QRect &rect) {
	const auto handle = reinterpret_cast<HWND>(widget->window()->winId());
	Expects(handle != nullptr);

	ComPtr<IVirtualDesktopManager> virtualDesktopManager;
	HRESULT hr = CoCreateInstance(
		CLSID_VirtualDesktopManager,
		nullptr,
		CLSCTX_ALL,
		IID_PPV_ARGS(&virtualDesktopManager));
	
	if (SUCCEEDED(hr)) {
		BOOL isCurrent;
		hr = virtualDesktopManager->IsWindowOnCurrentVirtualDesktop(
			handle,
			&isCurrent);
		if (SUCCEEDED(hr) && !isCurrent) {
			return true;
		}
	}

	std::vector<HWND> visited;
	const RECT nativeRect{
		rect.left(),
		rect.top(),
		rect.right(),
		rect.bottom(),
	};

	for (auto curHandle = handle;
		curHandle != nullptr && !ranges::contains(visited, curHandle);
		curHandle = GetWindow(curHandle, GW_HWNDPREV)) {
		visited.push_back(curHandle);
		if (curHandle == handle) {
			continue;
		}
		RECT testRect, intersection;
		if (IsWindowVisible(curHandle)
			&& GetWindowRect(curHandle, &testRect)
			&& IntersectRect(&intersection, &nativeRect, &testRect)) {
			return true;
		}
	}

	return false;
}

bool ShowWindowMenu(QWindow *window) {
	const auto pos = QCursor::pos();

	SendMessage(
		HWND(window->winId()),
		WM_SYSCOMMAND,
		SC_MOUSEMENU,
		MAKELPARAM(pos.x(), pos.y()));

	return true;
}

TitleControls::Layout TitleControlsLayout() {
	return TitleControls::Layout{
		.right = {
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
			TitleControls::Control::Close,
		}
	};
}

} // namespace Platform
} // namespace Ui
