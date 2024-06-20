// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_utility_win.h"

#include "ui/widgets/popup_menu.h"

#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtCore/QAbstractNativeEventFilter>

#include <windows.h>
#include <wrl/client.h>
#include <Shobjidl.h>

#include "base/event_filter.h"
#include <QTimer>
#include <QScreen>

using namespace Microsoft::WRL;

namespace Ui::Platform {

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
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
	const auto handle = HWND(widget->winId());
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

	const auto nativeRect = [&] {
		const auto topLeft = [&] {
			const auto qpoints = rect.topLeft()
				* widget->windowHandle()->devicePixelRatio();
			POINT result{
				qpoints.x(),
				qpoints.y(),
			};
			ClientToScreen(handle, &result);
			return result;
		}();
		const auto bottomRight = [&] {
			const auto qpoints = rect.bottomRight()
				* widget->windowHandle()->devicePixelRatio();
			POINT result{
				qpoints.x(),
				qpoints.y(),
			};
			ClientToScreen(handle, &result);
			return result;
		}();
		return RECT{
			topLeft.x,
			topLeft.y,
			bottomRight.x,
			bottomRight.y,
		};
	}();

	std::vector<HWND> visited;
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

void ShowWindowMenu(not_null<QWidget*> widget, const QPoint &point) {
	const auto handle = HWND(widget->winId());
	const auto mapped = point * widget->windowHandle()->devicePixelRatio();
	POINT p{ mapped.x(), mapped.y() };
	ClientToScreen(handle, &p);
	SendMessage(
		handle,
		0x313 /* WM_POPUPSYSTEMMENU */,
		0,
		MAKELPARAM(p.x, p.y));
}

void FixPopupMenuNativeEmojiPopup(not_null<PopupMenu*> menu) {
	// Windows native emoji selector, that can be called by Win+. shortcut,
	// is behaving strangely within an input field in a popup menu.
	//
	// When the selector is shown and a mouse button is pressed the system
	// sends two events "MousePress + MouseRelease" to the popup menu, even
	// before the button is physically released. That way we hide the menu
	// on this MousePress, that we shouldn't have received (in case of
	// input field in the main window no such events are sent at all).
	//
	// To workaround this we detect a WM_MOUSELEAVE event that is sent to
	// the popup menu when the selector is shown and skip all mouse press
	// events while we don't receive mouse move events. If we receive mouse
	// move events that means the selector was hidden and the mouse is
	// captured by the popup menu again.
	class Filter final : public QAbstractNativeEventFilter {
	public:
		explicit Filter(not_null<PopupMenu*> menu) : _menu(menu) {
		}

		bool nativeEventFilter(
				const QByteArray &eventType,
				void *message,
				native_event_filter_result *result) override {
			const auto msg = static_cast<MSG*>(message);
			switch (msg->message) {
			case WM_MOUSELEAVE: if (msg->hwnd == hwnd()) {
				_skipMouseDown = true;
			} break;
			case WM_MOUSEMOVE: if (msg->hwnd == hwnd()) {
				_skipMouseDown = false;
			} break;
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK: if (msg->hwnd == hwnd()) {
				return _skipMouseDown;
			}
			}
			return false;
		}

	private:
		[[nodiscard]] HWND hwnd() const {
			const auto top = _menu->window()->windowHandle();
			return top ? reinterpret_cast<HWND>(top->winId()) : nullptr;
		}

		not_null<PopupMenu*> _menu;
		bool _skipMouseDown = false;

	};

	QGuiApplication::instance()->installNativeEventFilter(
		menu->lifetime().make_state<Filter>(menu));
}

void SetGeometryWithPossibleScreenChange(
		not_null<QWidget*> widget,
		QRect geometry) {
	if (const auto screen = QGuiApplication::screenAt(geometry.center())) {
		const auto window = widget->window();
		window->createWinId();
		const auto handle = window->windowHandle();
		if (handle->screen() != screen) {
			handle->setScreen(screen);
			window->move(screen->availableGeometry().topLeft());
			window->show();
		}
	}
	widget->setGeometry(geometry);
}

} // namespace Ui::Platform
