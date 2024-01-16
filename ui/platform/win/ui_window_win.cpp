// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_window_win.h"

#include "ui/inactive_press.h"
#include "ui/platform/win/ui_window_title_win.h"
#include "ui/platform/win/ui_windows_direct_manipulation.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/rp_window.h"
#include "ui/widgets/elastic_scroll.h"
#include "base/platform/win/base_windows_safe_library.h"
#include "base/platform/base_platform_info.h"
#include "base/event_filter.h"
#include "base/integration.h"
#include "base/invoke_queued.h"
#include "base/debug_log.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QPoint>
#include <QtGui/QWindow>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QApplication>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qwindowsysteminterface.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

Q_DECLARE_METATYPE(QMargins);

namespace Ui::Platform {
namespace {

constexpr auto kDWMWCP_ROUND = DWORD(2);
constexpr auto kDWMWCP_DONOTROUND = DWORD(1);
constexpr auto kDWMWA_WINDOW_CORNER_PREFERENCE = DWORD(33);
constexpr auto kDWMWA_CAPTION_COLOR = DWORD(35);
constexpr auto kDWMWA_TEXT_COLOR = DWORD(36);

UINT(__stdcall *GetDpiForWindow)(_In_ HWND hwnd);

int(__stdcall *GetSystemMetricsForDpi)(
	_In_ int nIndex,
	_In_ UINT dpi);

BOOL(__stdcall *AdjustWindowRectExForDpi)(
	_Inout_ LPRECT lpRect,
	_In_ DWORD dwStyle,
	_In_ BOOL bMenu,
	_In_ DWORD dwExStyle,
	_In_ UINT dpi);

[[nodiscard]] bool GetDpiForWindowSupported() {
	static const auto Result = [&] {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto user32 = base::Platform::SafeLoadLibrary(L"User32.dll");
		return LOAD_SYMBOL(user32, GetDpiForWindow);
#undef LOAD_SYMBOL
	}();
	return Result;
}

[[nodiscard]] bool GetSystemMetricsForDpiSupported() {
	static const auto Result = [&] {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto user32 = base::Platform::SafeLoadLibrary(L"User32.dll");
		return LOAD_SYMBOL(user32, GetSystemMetricsForDpi);
#undef LOAD_SYMBOL
	}();
	return Result;
}

[[nodiscard]] bool AdjustWindowRectExForDpiSupported() {
	static const auto Result = [&] {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto user32 = base::Platform::SafeLoadLibrary(L"User32.dll");
		return LOAD_SYMBOL(user32, AdjustWindowRectExForDpi);
#undef LOAD_SYMBOL
	}();
	return Result;
}

[[nodiscard]] bool IsCompositionEnabled() {
	auto result = BOOL(FALSE);
	const auto success = (DwmIsCompositionEnabled(&result) == S_OK);
	return success && result;
}

[[nodiscard]] HWND FindTaskbarWindow(LPRECT rcMon = nullptr) {
	HWND hTaskbar = nullptr;
	RECT rcTaskbar, rcMatch;

	while ((hTaskbar = FindWindowEx(
		nullptr,
		hTaskbar,
		L"Shell_TrayWnd",
		nullptr)) != nullptr) {
		if (!rcMon) {
			break; // OK, return first found
		}
		if (GetWindowRect(hTaskbar, &rcTaskbar)
			&& IntersectRect(&rcMatch, &rcTaskbar, rcMon)) {
			break; // OK, taskbar match monitor
		}
	}

	return hTaskbar;
}

[[nodiscard]] bool IsTaskbarAutoHidden(
		LPRECT rcMon = nullptr,
		PUINT pEdge = nullptr) {
	HWND hTaskbar = FindTaskbarWindow(rcMon);
	if (!hTaskbar) {
		if (pEdge) {
			*pEdge = (UINT)-1;
		}
		return false;
	}

	APPBARDATA state = {sizeof(state), hTaskbar};
	APPBARDATA pos = {sizeof(pos), hTaskbar};

	LRESULT lState = SHAppBarMessage(ABM_GETSTATE, &state);
	bool bAutoHidden = (lState & ABS_AUTOHIDE);

	if (SHAppBarMessage(ABM_GETTASKBARPOS, &pos)) {
		if (pEdge) {
			*pEdge = pos.uEdge;
		}
	} else {
		DEBUG_LOG(("Failed to get taskbar pos"));
		if (pEdge) {
			*pEdge = ABE_BOTTOM;
		}
	}

	return bAutoHidden;
}

void FixAeroSnap(HWND handle) {
	SetWindowLongPtr(
		handle,
		GWL_STYLE,
		GetWindowLongPtr(handle, GWL_STYLE) | WS_CAPTION | WS_THICKFRAME);
}

[[nodiscard]] HWND ResolveWindowHandle(not_null<QWidget*> widget) {
	if (!::Platform::IsWindows8OrGreater()) {
		widget->setWindowFlag(Qt::FramelessWindowHint);
	}
	const auto result = GetWindowHandle(widget);
	if (!::Platform::IsWindows8OrGreater()) {
		FixAeroSnap(result);
	}
	return result;
}

[[nodiscard]] Qt::KeyboardModifiers LookupModifiers() {
	const auto check = [](int key) {
		return (GetKeyState(key) & 0x8000) != 0;
	};

	auto result = Qt::KeyboardModifiers();
	if (check(VK_SHIFT)) {
		result |= Qt::ShiftModifier;
	}
	// NB AltGr key (i.e., VK_RMENU on some keyboard layout) is not handled.
	if (check(VK_RMENU) || check(VK_MENU)) {
		result |= Qt::AltModifier;
	}
	if (check(VK_CONTROL)) {
		result |= Qt::ControlModifier;
	}
	if (check(VK_LWIN) || check(VK_RWIN)) {
		result |= Qt::MetaModifier;
	}
	return result;
}

} // namespace

class WindowHelper::NativeFilter final : public QAbstractNativeEventFilter {
public:
	void registerWindow(HWND handle, not_null<WindowHelper*> helper);
	void unregisterWindow(HWND handle);

	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) override;

private:
	base::flat_map<HWND, not_null<WindowHelper*>> _windowByHandle;

};

void WindowHelper::NativeFilter::registerWindow(
		HWND handle,
		not_null<WindowHelper*> helper) {
	_windowByHandle.emplace(handle, helper);
}

void WindowHelper::NativeFilter::unregisterWindow(HWND handle) {
	_windowByHandle.remove(handle);
}

bool WindowHelper::NativeFilter::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) {
	auto filtered = false;
	const auto msg = static_cast<MSG*>(message);
	const auto i = _windowByHandle.find(msg->hwnd);
	if (i != end(_windowByHandle)) {
		base::Integration::Instance().enterFromEventLoop([&] {
			filtered = i->second->handleNativeEvent(
				msg->message,
				msg->wParam,
				msg->lParam,
				reinterpret_cast<LRESULT*>(result));
		});
	}
	return filtered;
}

WindowHelper::WindowHelper(not_null<RpWidget*> window)
: BasicWindowHelper(window)
, _handle(ResolveWindowHandle(window))
, _title(Ui::CreateChild<TitleWidget>(window.get()))
, _body(Ui::CreateChild<RpWidget>(window.get()))
, _dpi(GetDpiForWindowSupported() ? GetDpiForWindow(_handle) : 0) {
	Expects(_handle != nullptr);

	init();
}

WindowHelper::~WindowHelper() {
	GetNativeFilter()->unregisterWindow(_handle);
}

void WindowHelper::initInWindow(not_null<RpWindow*> window) {
	_title->initInWindow(window);
}

not_null<RpWidget*> WindowHelper::body() {
	return _body;
}

QMargins WindowHelper::frameMargins() {
	return _title->isHidden()
		? BasicWindowHelper::nativeFrameMargins()
		: QMargins{ 0, _title->height(), 0, 0 };
}

int WindowHelper::additionalContentPadding() const {
	return _title->isHidden() ? 0 : _title->additionalPadding();
}

rpl::producer<int> WindowHelper::additionalContentPaddingValue() const {
	return rpl::combine(
		_title->shownValue(),
		_title->additionalPaddingValue()
	) | rpl::map([](bool shown, int padding) {
		return shown ? padding : 0;
	}) | rpl::distinct_until_changed();
}

void WindowHelper::setTitle(const QString &title) {
	_title->setText(title);
	window()->setWindowTitle(title);
}

void WindowHelper::setTitleStyle(const style::WindowTitle &st) {
	_title->setStyle(st);
	updateWindowFrameColors();
}

void WindowHelper::setNativeFrame(bool enabled) {
	if (!::Platform::IsWindows8OrGreater()) {
		window()->windowHandle()->setFlag(Qt::FramelessWindowHint, !enabled);
		if (!enabled) {
			FixAeroSnap(_handle);
		}
	}
	_title->setVisible(!enabled);
	if (enabled || ::Platform::IsWindows11OrGreater()) {
		_shadow.reset();
	} else {
		_shadow.emplace(window(), st::windowShadowFg->c);
		_shadow->setResizeEnabled(!fixedSize());
		initialShadowUpdate();
	}
	updateCornersRounding();
	updateMargins();
	updateWindowFrameColors();
	fixMaximizedWindow();
	SetWindowPos(
		_handle,
		0,
		0,
		0,
		0,
		0,
		SWP_FRAMECHANGED
			| SWP_NOMOVE
			| SWP_NOSIZE
			| SWP_NOZORDER
			| SWP_NOACTIVATE);
}

void WindowHelper::initialShadowUpdate() {
	if (!_shadow) {
		return;
	}
	using Change = WindowShadow::Change;
	const auto noShadowStates = (Qt::WindowMinimized | Qt::WindowMaximized);
	if ((window()->windowState() & noShadowStates) || window()->isHidden()) {
		_shadow->update(Change::Hidden);
	} else {
		_shadow->update(Change::Moved | Change::Resized | Change::Shown);
	}
}

void WindowHelper::updateCornersRounding() {
	if (!::Platform::IsWindows11OrGreater()) {
		return;
	}
	const auto preference = (_isFullScreen || _isMaximizedAndTranslucent)
		? kDWMWCP_DONOTROUND
		: kDWMWCP_ROUND;
	DwmSetWindowAttribute(
		_handle,
		kDWMWA_WINDOW_CORNER_PREFERENCE,
		&preference,
		sizeof(preference));
}

void WindowHelper::setMinimumSize(QSize size) {
	window()->setMinimumSize(size.width(), titleHeight() + size.height());
}

void WindowHelper::setFixedSize(QSize size) {
	window()->setFixedSize(size.width(), titleHeight() + size.height());
	_title->setResizeEnabled(false);
	if (_shadow) {
		_shadow->setResizeEnabled(false);
	}
}

void WindowHelper::setGeometry(QRect rect) {
	SetGeometryWithPossibleScreenChange(
		window(),
		rect.marginsAdded({ 0, titleHeight(), 0, 0 }));
}

void WindowHelper::showFullScreen() {
	if (!_isFullScreen) {
		_isFullScreen = true;
		updateMargins();
		updateCornersRounding();
		updateCloaking();
	}
	window()->showFullScreen();
}

void WindowHelper::showNormal() {
	window()->showNormal();
	if (_isFullScreen) {
		_isFullScreen = false;
		updateMargins();
		updateCornersRounding();
		updateCloaking();
	}
}

auto WindowHelper::hitTestRequests() const
-> rpl::producer<not_null<HitTestRequest*>> {
	return _hitTestRequests.events();
}

rpl::producer<HitTestResult> WindowHelper::systemButtonOver() const {
	return _systemButtonOver.events();
}

rpl::producer<HitTestResult> WindowHelper::systemButtonDown() const {
	return _systemButtonDown.events();
}

void WindowHelper::overrideSystemButtonOver(HitTestResult button) {
	_systemButtonOver.fire_copy(button);
}

void WindowHelper::overrideSystemButtonDown(HitTestResult button) {
	_systemButtonDown.fire_copy(button);
}

void WindowHelper::init() {
	_title->show();
	GetNativeFilter()->registerWindow(_handle, this);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		if (_shadow) {
			_shadow->setColor(st::windowShadowFg->c);
		}
		updateWindowFrameColors();
		Ui::ForceFullRepaint(window());
	}, window()->lifetime());

	rpl::combine(
		window()->sizeValue(),
		_title->heightValue(),
		_title->shownValue()
	) | rpl::start_with_next([=](
			QSize size,
			int titleHeight,
			bool titleShown) {
		_body->setGeometry(
			0,
			titleShown ? titleHeight : 0,
			size.width(),
			size.height() - (titleShown ? titleHeight : 0));
	}, _body->lifetime());

	hitTestRequests(
	) | rpl::filter([=](not_null<HitTestRequest*> request) {
		return ::Platform::IsWindows11OrGreater();
	}) | rpl::start_with_next([=](not_null<HitTestRequest*> request) {
		request->result = [=] {
			const auto maximized = window()->isMaximized()
				|| window()->isFullScreen();
			const auto px = int(std::ceil(
				st::windowTitleHeight
					* window()->windowHandle()->devicePixelRatio()
					/ 10.));
			return (!maximized && (request->point.y() < px))
				? HitTestResult::Top
				: HitTestResult::Client;
		}();
	}, window()->lifetime());

	_dpi.value() | rpl::start_with_next([=](uint dpi) {
		updateMargins();
	}, window()->lifetime());

	if (!::Platform::IsWindows11OrGreater()) {
		_shadow.emplace(window(), st::windowShadowFg->c);
	}

	if (!::Platform::IsWindows8OrGreater()) {
		SetWindowTheme(_handle, L" ", L" ");
		QApplication::setStyle(QStyleFactory::create("Windows"));
	}

	updateWindowFrameColors();

	const auto handleStateChanged = [=](Qt::WindowState state) {
		if (fixedSize() && (state & Qt::WindowMaximized)) {
			crl::on_main(window().get(), [=] {
				window()->setWindowState(
					window()->windowState() & ~Qt::WindowMaximized);
			});
		}
		if (state != Qt::WindowMinimized) {
			const auto is = (state == Qt::WindowMaximized)
				&& window()->testAttribute(Qt::WA_TranslucentBackground);
			if (_isMaximizedAndTranslucent != is) {
				_isMaximizedAndTranslucent = is;
				updateCornersRounding();
			}
		}
	};
	Ui::Connect(
		window()->windowHandle(),
		&QWindow::windowStateChanged,
		handleStateChanged);

	initialShadowUpdate();
	updateCornersRounding();

	auto dm = std::make_unique<DirectManipulation>(window());
	if (dm->valid()) {
		_directManipulation = std::move(dm);
		_directManipulation->events(
		) | rpl::start_with_next([=](const DirectManipulationEvent &event) {
			handleDirectManipulationEvent(event);
		}, window()->lifetime());
	}

	window()->shownValue() | rpl::filter([=](bool shown) {
		return !shown;
	}) | rpl::start_with_next([=] {
		updateCloaking();

		const auto qwindow = window()->windowHandle();
		const auto firstPaintEventFilter = std::make_shared<QObject*>();
		*firstPaintEventFilter = base::install_event_filter(
			qwindow,
			[=](not_null<QEvent*> e) {
				if (e->type() == QEvent::Expose && qwindow->isExposed()) {
					InvokeQueued(qwindow, [=] {
						InvokeQueued(qwindow, [=] {
							updateCloaking();
						});
					});
					delete base::take(*firstPaintEventFilter);
				}
				return base::EventFilterResult::Continue;
			});
	}, window()->lifetime());
}

void WindowHelper::handleDirectManipulationEvent(
		const DirectManipulationEvent &event) {
	using Type = DirectManipulationEventType;
	const auto send = [&](Qt::ScrollPhase phase) {
		if (const auto windowHandle = window()->windowHandle()) {
			auto global = POINT();
			::GetCursorPos(&global);
			auto local = global;
			::ScreenToClient(_handle, &local);
			const auto dpi = _dpi.current() ? double(_dpi.current()) : 96.;
			const auto delta = QPointF(event.delta) / (dpi / 96.);
			QWindowSystemInterface::handleWheelEvent(
				windowHandle,
				QPointF(local.x, local.y),
				QPointF(global.x, global.y),
				delta.toPoint(),
				(delta * kPixelToAngleDelta).toPoint(),
				LookupModifiers(),
				phase,
				Qt::MouseEventSynthesizedBySystem);
		}
	};
	switch (event.type) {
	case Type::ScrollStart: send(Qt::ScrollBegin); break;
	case Type::Scroll: send(Qt::ScrollUpdate); break;
	case Type::FlingStart:
	case Type::Fling: send(Qt::ScrollMomentum); break;
	case Type::ScrollStop: send(Qt::ScrollEnd); break;
	case Type::FlingStop: send(Qt::ScrollEnd); break;
	}
}

bool WindowHelper::handleNativeEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	if (handleSystemButtonEvent(msg, wParam, lParam, result)) {
		return true;
	}

	switch (msg) {

	case WM_ACTIVATE: {
		if (LOWORD(wParam) == WA_CLICKACTIVE) {
			Ui::MarkInactivePress(window(), true);
		}
		const auto active = (LOWORD(wParam) != WA_INACTIVE);
		if (_shadow) {
			if (active) {
				_shadow->update(WindowShadow::Change::Activate);
			} else {
				_shadow->update(WindowShadow::Change::Deactivate);
			}
		}
		updateWindowFrameColors(active);
		window()->update();
	} return false;

	case WM_NCPAINT: {
		if (::Platform::IsWindows8OrGreater() || _title->isHidden()) {
			return false;
		}
		if (result) *result = 0;
	} return true;

	case WM_NCCALCSIZE: {
		if (_title->isHidden() || window()->isFullScreen() || !wParam) {
			return false;
		}
		const auto r = &((LPNCCALCSIZE_PARAMS)lParam)->rgrc[0];
		const auto maximized = [&] {
			WINDOWPLACEMENT wp;
			wp.length = sizeof(WINDOWPLACEMENT);
			return GetWindowPlacement(_handle, &wp)
				&& (wp.showCmd == SW_SHOWMAXIMIZED);
		}();
		const auto addBorders = maximized
			|| ::Platform::IsWindows11OrGreater();
		if (addBorders) {
			const auto dpi = _dpi.current();
			const auto borderWidth = (GetSystemMetricsForDpiSupported() && dpi)
				? GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
					+ GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
				: GetSystemMetrics(SM_CXSIZEFRAME)
					+ GetSystemMetrics(SM_CXPADDEDBORDER);
			const auto borderHeight = (GetSystemMetricsForDpiSupported() && dpi)
				? GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
					+ GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
				: GetSystemMetrics(SM_CYSIZEFRAME)
					+ GetSystemMetrics(SM_CXPADDEDBORDER);
			r->left += borderWidth;
			r->right -= borderWidth;
			if (maximized) {
				r->top += borderHeight;
			}
			r->bottom -= borderHeight;
		}
		if (maximized) {
			const auto hMonitor = MonitorFromWindow(
				_handle,
				MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi;
			mi.cbSize = sizeof(mi);
			UINT uEdge = (UINT)-1;
			if (GetMonitorInfo(hMonitor, &mi)
				&& IsTaskbarAutoHidden(&mi.rcMonitor, &uEdge)) {
				switch (uEdge) {
				case ABE_LEFT: r->left += 1; break;
				case ABE_RIGHT: r->right -= 1; break;
				case ABE_TOP: r->top += 1; break;
				case ABE_BOTTOM: r->bottom -= 1; break;
				}
			}
		}
		if (result) *result = addBorders ? 0 : WVR_REDRAW;
	} return true;

	case WM_NCRBUTTONUP: {
		if (_title->isHidden()) {
			return false;
		}
		POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(_handle, &p);
		const auto mapped = QPoint(p.x, p.y)
			/ window()->windowHandle()->devicePixelRatio();
		ShowWindowMenu(window(), mapped);
		if (result) *result = 0;
	} return true;

	case WM_NCACTIVATE: {
		if (_title->isHidden()) {
			return false;
		}
		if (IsCompositionEnabled()) {
			const auto res = DefWindowProc(_handle, msg, wParam, -1);
			if (result) *result = res;
		} else {
			// Thanks https://github.com/melak47/BorderlessWindow
			if (result) *result = 1;
		}
	} return true;

	case WM_WINDOWPOSCHANGING:
	case WM_WINDOWPOSCHANGED: {
		auto placement = WINDOWPLACEMENT{
			.length = sizeof(WINDOWPLACEMENT),
		};
		if (!GetWindowPlacement(_handle, &placement)) {
			LOG(("System Error: GetWindowPlacement failed."));
			return false;
		}
		_title->refreshAdditionalPaddings(_handle, placement);
		if (_shadow) {
			if (placement.showCmd == SW_SHOWMAXIMIZED
				|| placement.showCmd == SW_SHOWMINIMIZED) {
				_shadow->update(WindowShadow::Change::Hidden);
			} else {
				_shadow->update(
					WindowShadow::Change::Moved | WindowShadow::Change::Resized,
					(WINDOWPOS*)lParam);
			}
		}
	} return false;

	case WM_SIZE: {
		if (wParam == SIZE_MAXIMIZED
			|| wParam == SIZE_RESTORED
			|| wParam == SIZE_MINIMIZED) {
			const auto now = window()->windowState();
			if (wParam != SIZE_RESTORED
				|| (now != Qt::WindowNoState
					&& now != Qt::WindowFullScreen)) {
				Qt::WindowState state = Qt::WindowNoState;
				if (wParam == SIZE_MAXIMIZED) {
					state = Qt::WindowMaximized;
				} else if (wParam == SIZE_MINIMIZED) {
					state = Qt::WindowMinimized;
				}
				window()->windowHandle()->windowStateChanged(state);
			}
			updateMargins();
			_title->refreshAdditionalPaddings(_handle);
			if (_shadow) {
				const auto changes = (wParam == SIZE_MINIMIZED
					|| wParam == SIZE_MAXIMIZED)
					? WindowShadow::Change::Hidden
					: (WindowShadow::Change::Resized
						| WindowShadow::Change::Shown);
				_shadow->update(changes);
			}
		}
	} return false;

	case WM_SHOWWINDOW: {
		if (_shadow) {
			const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
			const auto changes = WindowShadow::Change::Resized
				| ((wParam && !(style & (WS_MAXIMIZE | WS_MINIMIZE)))
					? WindowShadow::Change::Shown
					: WindowShadow::Change::Hidden);
			_shadow->update(changes);
		}
	} return false;

	case WM_MOVE: {
		_title->refreshAdditionalPaddings(_handle);
		if (_shadow) {
			_shadow->update(WindowShadow::Change::Moved);
		}
	} return false;

	case WM_NCHITTEST: {
		if (!result || _title->isHidden()) {
			return false;
		}

		POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(_handle, &p);
		const auto ratio = window()->windowHandle()->devicePixelRatio();
		const auto mapped = QPoint(
			int(std::floor(p.x / ratio)),
			int(std::floor(p.y / ratio)));
		*result = [&]() -> LRESULT {
			if (!window()->rect().contains(mapped)) {
				return DefWindowProc(_handle, msg, wParam, lParam);
			}
			auto request = HitTestRequest{
				.point = mapped,
			};
			_hitTestRequests.fire(&request);
			switch (const auto result = request.result) {
			case HitTestResult::Client:      return HTCLIENT;
			case HitTestResult::Caption:     return HTCAPTION;
			case HitTestResult::Top:         return HTTOP;
			case HitTestResult::TopRight:    return HTTOPRIGHT;
			case HitTestResult::Right:       return HTRIGHT;
			case HitTestResult::BottomRight: return HTBOTTOMRIGHT;
			case HitTestResult::Bottom:      return HTBOTTOM;
			case HitTestResult::BottomLeft:  return HTBOTTOMLEFT;
			case HitTestResult::Left:        return HTLEFT;
			case HitTestResult::TopLeft:     return HTTOPLEFT;

			case HitTestResult::Minimize:
			case HitTestResult::MaximizeRestore:
			case HitTestResult::Close: return systemButtonHitTest(result);

			case HitTestResult::None:
			default: return DefWindowProc(_handle, msg, wParam, lParam);
			};
		}();
		_systemButtonOver.fire(systemButtonHitTest(*result));
	} return true;

	case WM_DPICHANGED: {
		_dpi = LOWORD(wParam);
		InvokeQueued(_title, [=] {
			_title->refreshAdditionalPaddings(_handle);
		});
	} return false;

	case DM_POINTERHITTEST: {
		if (_directManipulation) {
			_directManipulation->handlePointerHitTest(wParam);
			return true;
		}
	} return false;

	}
	return false;
}

bool WindowHelper::fixedSize() const {
	return window()->minimumSize() == window()->maximumSize();
}

bool WindowHelper::handleSystemButtonEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	if (_title->isHidden()) {
		return false;
	}
	const auto testResult = LOWORD(wParam);
	const auto sysButtons = { HTMINBUTTON, HTMAXBUTTON, HTCLOSE };
	const auto overSysButton = ranges::contains(sysButtons, testResult);
	switch (msg) {
	case WM_NCLBUTTONDBLCLK:
	case WM_NCMBUTTONDBLCLK:
	case WM_NCRBUTTONDBLCLK:
	case WM_NCXBUTTONDBLCLK: {
		if (!overSysButton || fixedSize()) {
			return false;
		}
		// Ignore double clicks on system buttons.
		if (result) *result = 0;
	} return true;

	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONUP:
		_systemButtonDown.fire((msg == WM_NCLBUTTONDOWN)
			? systemButtonHitTest(testResult)
			: HitTestResult::None);
		if (overSysButton) {
			if (result) *result = 0;
		}
		return overSysButton;
	case WM_NCMBUTTONDOWN:
	case WM_NCMBUTTONUP:
	case WM_NCRBUTTONDOWN:
	case WM_NCRBUTTONUP:
	case WM_NCXBUTTONDOWN:
	case WM_NCXBUTTONUP:
		if (!overSysButton) {
			return false;
		}
		if (result) *result = 0;
		return true;
	case WM_NCMOUSEHOVER:
	case WM_NCMOUSEMOVE:
		_systemButtonOver.fire(systemButtonHitTest(testResult));
		if (overSysButton) {
			if (result) *result = 0;
		}
		return overSysButton;
	case WM_NCMOUSELEAVE:
		_systemButtonOver.fire(HitTestResult::None);
		return false;
	}
	return false;
}

int WindowHelper::systemButtonHitTest(HitTestResult result) const {
	if (!SemiNativeSystemButtonProcessing()) {
		return HTCLIENT;
	}
	switch (result) {
	case HitTestResult::Minimize: return HTMINBUTTON;
	case HitTestResult::MaximizeRestore: return HTMAXBUTTON;
	case HitTestResult::Close: return HTCLOSE;
	}
	return HTTRANSPARENT;
}

HitTestResult WindowHelper::systemButtonHitTest(int result) const {
	if (!SemiNativeSystemButtonProcessing()) {
		return HitTestResult::None;
	}
	switch (result) {
	case HTMINBUTTON: return HitTestResult::Minimize;
	case HTMAXBUTTON: return HitTestResult::MaximizeRestore;
	case HTCLOSE: return HitTestResult::Close;
	}
	return HitTestResult::None;
}

int WindowHelper::titleHeight() const {
	return _title->isHidden() ? 0 : _title->height();
}

void WindowHelper::updateWindowFrameColors() {
	updateWindowFrameColors(window()->isActiveWindow());
}

void WindowHelper::updateWindowFrameColors(bool active) {
	if (!::Platform::IsWindows11OrGreater()) {
		return;
	}
	const auto bg = active
		? _title->st()->bgActive->c
		: _title->st()->bg->c;
	COLORREF bgRef = RGB(bg.red(), bg.green(), bg.blue());
	DwmSetWindowAttribute(
		_handle,
		kDWMWA_CAPTION_COLOR,
		&bgRef,
		sizeof(COLORREF));
	const auto fg = active
		? _title->st()->fgActive->c
		: _title->st()->fg->c;
	COLORREF fgRef = RGB(fg.red(), fg.green(), fg.blue());
	DwmSetWindowAttribute(
		_handle,
		kDWMWA_TEXT_COLOR,
		&fgRef,
		sizeof(COLORREF));
}

void WindowHelper::updateCloaking() {
	const auto enabled = window()->isHidden() && !_isFullScreen;
	const auto flag = BOOL(enabled ? TRUE : FALSE);
	DwmSetWindowAttribute(_handle, DWMWA_CLOAK, &flag, sizeof(flag));
}

void WindowHelper::updateMargins() {
	if (_updatingMargins) return;

	_updatingMargins = true;
	const auto guard = gsl::finally([&] { _updatingMargins = false; });

	RECT r{};
	const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
	const auto styleEx = GetWindowLongPtr(_handle, GWL_EXSTYLE);
	const auto dpi = _dpi.current();
	if (AdjustWindowRectExForDpiSupported() && dpi) {
		AdjustWindowRectExForDpi(&r, style, false, styleEx, dpi);
	} else {
		AdjustWindowRectEx(&r, style, false, styleEx);
	}
	auto margins = ::Platform::IsWindows11OrGreater()
		? QMargins(0, r.top, 0, 0)
		: QMargins(r.left, r.top, -r.right, -r.bottom);
	if (style & WS_MAXIMIZE) {
		RECT w, m;
		GetWindowRect(_handle , &w);
		m = w;

		HMONITOR hMonitor = MonitorFromRect(&w, MONITOR_DEFAULTTONEAREST);
		if (hMonitor) {
			MONITORINFO mi;
			mi.cbSize = sizeof(mi);
			GetMonitorInfo(hMonitor, &mi);
			m = mi.rcWork;
		}

		_marginsDelta = QMargins(
			w.left - m.left,
			w.top - m.top,
			m.right - w.right,
			m.bottom - w.bottom);

		margins.setLeft(margins.left() - _marginsDelta.left());
		margins.setRight(margins.right() - _marginsDelta.right());
		margins.setBottom(margins.bottom() - _marginsDelta.bottom());
		margins.setTop(margins.top() - _marginsDelta.top());
	} else if (!_marginsDelta.isNull()) {
		RECT w;
		GetWindowRect(_handle, &w);
		SetWindowPos(
			_handle,
			0,
			0,
			0,
			w.right - w.left - _marginsDelta.left() - _marginsDelta.right(),
			w.bottom - w.top - _marginsDelta.top() - _marginsDelta.bottom(),
			(SWP_NOMOVE
				| SWP_NOSENDCHANGING
				| SWP_NOZORDER
				| SWP_NOACTIVATE
				| SWP_NOREPOSITION));
		_marginsDelta = QMargins();
	}

	if (_isFullScreen || _title->isHidden()) {
		margins = QMargins();
		if (_title->isHidden()) {
			_marginsDelta = QMargins();
		}
	}
	if (const auto native = QGuiApplication::platformNativeInterface()) {
		native->setWindowProperty(
			window()->windowHandle()->handle(),
			"WindowsCustomMargins",
			QVariant::fromValue<QMargins>(margins));
	}
}

void WindowHelper::fixMaximizedWindow() {
	const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
	if (style & WS_MAXIMIZE) {
		auto w = RECT();
		GetWindowRect(_handle, &w);
		if (const auto hMonitor = MonitorFromRect(&w, MONITOR_DEFAULTTONEAREST)) {
			MONITORINFO mi;
			mi.cbSize = sizeof(mi);
			GetMonitorInfo(hMonitor, &mi);
			const auto m = mi.rcWork;
			SetWindowPos(_handle, 0, 0, 0, m.right - m.left - _marginsDelta.left() - _marginsDelta.right(), m.bottom - m.top - _marginsDelta.top() - _marginsDelta.bottom(), SWP_NOMOVE | SWP_NOSENDCHANGING | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREPOSITION);
		}
	}
}

not_null<WindowHelper::NativeFilter*> WindowHelper::GetNativeFilter() {
	Expects(QCoreApplication::instance() != nullptr);

	static const auto GlobalFilter = [&] {
		const auto application = QCoreApplication::instance();
		const auto filter = Ui::CreateChild<NativeFilter>(application);
		application->installNativeEventFilter(filter);
		return filter;
	}();
	return GlobalFilter;
}

HWND GetWindowHandle(not_null<QWidget*> widget) {
	const auto toplevel = widget->window();
	toplevel->createWinId();
	return GetWindowHandle(toplevel->windowHandle());
}

HWND GetWindowHandle(not_null<QWindow*> window) {
	return reinterpret_cast<HWND>(window->winId());
}

void SendWMPaintForce(not_null<QWidget*> widget) {
	const auto toplevel = widget->window();
	toplevel->createWinId();
	SendWMPaintForce(toplevel->windowHandle());
}

void SendWMPaintForce(not_null<QWindow*> window) {
	::InvalidateRect(GetWindowHandle(window), nullptr, FALSE);
}

std::unique_ptr<BasicWindowHelper> CreateSpecialWindowHelper(
		not_null<RpWidget*> window) {
	return std::make_unique<WindowHelper>(window);
}

bool NativeWindowFrameSupported() {
	return true;
}

} // namespace Ui::Platform
