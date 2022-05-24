// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_window_win.h"

#include "ui/inactive_press.h"
#include "ui/platform/win/ui_window_title_win.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/rp_window.h"
#include "ui/painter.h"
#include "base/platform/win/base_windows_safe_library.h"
#include "base/platform/base_platform_info.h"
#include "base/integration.h"
#include "base/debug_log.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtGui/QWindow>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QApplication>

#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

namespace Ui {
namespace Platform {
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
, _handle(GetWindowHandle(window))
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
	return frameMarginsSet()
		? *_frameMargins.current()
		: _title->isHidden()
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
		const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
		SetWindowLongPtr(
			_handle,
			GWL_STYLE,
			enabled ? (style | WS_CAPTION) : (style & ~WS_CAPTION));
	}
	if (::Platform::IsWindows11OrGreater()) {
		if (enabled) {
			_frameMargins = QMargins();
			updateFrameMargins();
		} else {
			_frameMargins = std::nullopt;
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
	updateWindowFrameColors();
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
	auto preference = _isFullScreen ? kDWMWCP_DONOTROUND : kDWMWCP_ROUND;
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
	window()->setGeometry(rect.marginsAdded({ 0, titleHeight(), 0, 0 }));
}

void WindowHelper::showFullScreen() {
	if (!_isFullScreen) {
		_isFullScreen = true;
		updateCornersRounding();
	}
	window()->showFullScreen();
}

void WindowHelper::showNormal() {
	window()->showNormal();
	if (_isFullScreen) {
		_isFullScreen = false;
		updateCornersRounding();
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
		_title->shownValue(),
		_frameMargins.value()
	) | rpl::start_with_next([=](
			QSize size,
			int titleHeight,
			bool titleShown,
			std::optional<QMargins> frameMargins) {
		_body->setGeometry(QRect(
			0,
			0,
			size.width(),
			size.height()
		).marginsRemoved(QMargins(
			0,
			titleShown ? titleHeight : 0,
			0,
			0
		)).marginsRemoved(frameMargins ? *frameMargins : QMargins()));
	}, _body->lifetime());

	hitTestRequests(
	) | rpl::filter([=](not_null<HitTestRequest*> request) {
		return ::Platform::IsWindows11OrGreater();
	}) | rpl::start_with_next([=](not_null<HitTestRequest*> request) {
		request->result = [=] {
			RECT r{};
			const auto style = GetWindowLongPtr(_handle, GWL_STYLE)
				& ~WS_CAPTION;
			const auto styleEx = GetWindowLongPtr(_handle, GWL_EXSTYLE);
			const auto dpi = frameMarginsSet()
				? _dpi.current()
				: style::ConvertScale(96 * style::DevicePixelRatio());
			if (AdjustWindowRectExForDpiSupported() && dpi) {
				AdjustWindowRectExForDpi(&r, style, false, styleEx, dpi);
			} else {
				AdjustWindowRectEx(&r, style, false, styleEx);
			}
			const auto frameMargins = _frameMargins.current()
				? *_frameMargins.current()
				: QMargins();
			const auto maximized = window()->isMaximized()
				|| window()->isFullScreen();
			return (!maximized && (request->point.y() < -r.top))
				? HitTestResult::Top
				: (request->point.y() < frameMargins.top())
				? HitTestResult::Caption
				: HitTestResult::Client;
		}();
	}, window()->lifetime());

	_dpi.value(
	) | rpl::start_with_next([=](uint dpi) {
		updateFrameMargins();
	}, window()->lifetime());

	_frameMargins.value(
	) | rpl::start_with_next([=](std::optional<QMargins> frameMargins) {
		const auto deviceDependentMargins = frameMargins
			? *frameMargins * window()->devicePixelRatioF()
			: QMargins();
		const MARGINS m{
			deviceDependentMargins.left(),
			deviceDependentMargins.right(),
			deviceDependentMargins.top(),
			deviceDependentMargins.bottom(),
		};
		DwmExtendFrameIntoClientArea(_handle, &m);
	}, window()->lifetime());

	window()->paintRequest(
	) | rpl::filter([=](QRect clip) {
		return frameMarginsSet() && clip.intersects(QRect(
			0,
			0,
			window()->width(),
			_frameMargins.current()->top()));
	}) | rpl::start_with_next([=] {
		Painter p(window());
		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.fillRect(
			0,
			0,
			window()->width(),
			_frameMargins.current()->top(),
			Qt::black);
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
	};
	Ui::Connect(
		window()->windowHandle(),
		&QWindow::windowStateChanged,
		handleStateChanged);

	initialShadowUpdate();
	updateCornersRounding();
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
		if ((_title->isHidden() && !frameMarginsSet())
			|| window()->isFullScreen()
			|| !wParam) {
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
			const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
			const auto borderWidth = ((GetSystemMetricsForDpiSupported() && dpi)
				? GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
					+ GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
				: GetSystemMetrics(SM_CXSIZEFRAME)
					+ GetSystemMetrics(SM_CXPADDEDBORDER))
				- ((style & WS_CAPTION) ? 0 : 1);
			r->left += borderWidth;
			r->right -= borderWidth;
			if (maximized && !frameMarginsSet()) {
				r->top += borderWidth;
			}
			r->bottom -= borderWidth;
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
				if (!frameMarginsSet()) {
					case ABE_TOP: r->top += 1; break;
				}
				case ABE_BOTTOM: r->bottom -= 1; break;
				}
			}
		}
		if (result) *result = addBorders ? 0 : WVR_REDRAW;
	} return true;

	case WM_NCRBUTTONUP: {
		if (_title->isHidden() && !frameMarginsSet()) {
			return false;
		}
		POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(_handle, &p);
		const auto mapped = QPoint(p.x, p.y) / window()->devicePixelRatioF();
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
			if (wParam != SIZE_RESTORED
				|| window()->windowState() != Qt::WindowNoState) {
				Qt::WindowState state = Qt::WindowNoState;
				if (wParam == SIZE_MAXIMIZED) {
					state = Qt::WindowMaximized;
				} else if (wParam == SIZE_MINIMIZED) {
					state = Qt::WindowMinimized;
				}
				window()->windowHandle()->windowStateChanged(state);
			}
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
		const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
		if (!::Platform::IsWindows8OrGreater()
			&& !_title->isHidden()
			&& (style & WS_CAPTION)) {
			SetWindowLongPtr(
				_handle,
				GWL_STYLE,
				style & ~WS_CAPTION);
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
		if (!result) {
			return false;
		} else if (frameMarginsSet()) {
			LRESULT lRet = 0;
			DwmDefWindowProc(_handle, msg, wParam, lParam, &lRet);
			if (lRet) {
				*result = lRet;
				return true;
			}
		} else if (_title->isHidden()) {
			return false;
		}

		POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(_handle, &p);
		const auto mapped = QPoint(p.x, p.y) / window()->devicePixelRatioF();
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

	case WM_NCMOUSELEAVE: {
		if (result && frameMarginsSet()) {
			return DwmDefWindowProc(_handle, msg, wParam, lParam, result);
		}
	} return false;

	// should return true for Qt not to change window size
	// when moving the window between screens
	// change to false once runtime scale change would be supported
	case WM_DPICHANGED: {
		_dpi = LOWORD(wParam);
	} return true;

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
	return frameMarginsSet()
		? _frameMargins.current()->top()
		: _title->isHidden()
		? 0
		: _title->height();
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

bool WindowHelper::frameMarginsSet() const {
	return _frameMargins.current() && !_frameMargins.current()->isNull();
}

void WindowHelper::updateFrameMargins() {
	if (!_frameMargins.current()) {
		return;
	}
	RECT r{};
	const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
	const auto styleEx = GetWindowLongPtr(_handle, GWL_EXSTYLE);
	if (AdjustWindowRectExForDpiSupported() && _dpi.current()) {
		AdjustWindowRectExForDpi(&r, style, false, styleEx, _dpi.current());
	} else {
		AdjustWindowRectEx(&r, style, false, styleEx);
	}
	_frameMargins = QMargins(0, -r.top, 0, 0) / window()->devicePixelRatioF();
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

} // namespace Platform
} // namespace Ui
