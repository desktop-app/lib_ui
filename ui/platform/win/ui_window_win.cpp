// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_window_win.h"

#include "ui/inactive_press.h"
#include "ui/platform/win/ui_window_title_win.h"
#include "ui/widgets/rp_window.h"
#include "base/platform/base_platform_info.h"
#include "base/integration.h"
#include "base/debug_log.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtGui/QWindow>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QApplication>
#include <qpa/qplatformnativeinterface.h>

#include <dwmapi.h>
#include <uxtheme.h>

Q_DECLARE_METATYPE(QMargins);

namespace Ui {
namespace Platform {
namespace {

constexpr auto kDWMWCP_ROUND = DWORD(2);
constexpr auto kDWMWCP_DONOTROUND = DWORD(1);
constexpr auto kDWMWA_WINDOW_CORNER_PREFERENCE = DWORD(33);
constexpr auto kDWMWA_CAPTION_COLOR = DWORD(35);
constexpr auto kDWMWA_TEXT_COLOR = DWORD(36);

[[nodiscard]] bool IsCompositionEnabled() {
	auto result = BOOL(FALSE);
	const auto success = (DwmIsCompositionEnabled(&result) == S_OK);
	return success && result;
}

HWND FindTaskbarWindow(LPRECT rcMon = nullptr) {
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

bool IsTaskbarAutoHidden(LPRECT rcMon = nullptr, PUINT pEdge = nullptr) {
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
, _shadow(std::in_place, window, st::windowShadowFg->c) {
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
	if (enabled) {
		_shadow.reset();
	} else {
		_shadow.emplace(window(), st::windowShadowFg->c);
		_shadow->setResizeEnabled(!fixedSize());
		initialShadowUpdate();
	}
	updateMargins();
	updateWindowFrameColors();
	fixMaximizedWindow();
}

void WindowHelper::initialShadowUpdate() {
	using Change = WindowShadow::Change;
	const auto noShadowStates = (Qt::WindowMinimized | Qt::WindowMaximized);
	if ((window()->windowState() & noShadowStates) || window()->isHidden()) {
		_shadow->update(Change::Hidden);
	} else {
		_shadow->update(Change::Moved | Change::Resized | Change::Shown);
	}
	updateCornersRounding();
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
		updateMargins();
		updateCornersRounding();
	}
	window()->showFullScreen();
}

void WindowHelper::showNormal() {
	window()->showNormal();
	if (_isFullScreen) {
		_isFullScreen = false;
		updateMargins();
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

	updateMargins();

	if (!::Platform::IsWindows8OrGreater()) {
		SetWindowTheme(_handle, L" ", L" ");
		QApplication::setStyle(QStyleFactory::create("Windows"));
	}
	updateWindowFrameColors();

	_menu = GetSystemMenu(_handle, FALSE);
	updateSystemMenu();

	const auto handleStateChanged = [=](Qt::WindowState state) {
		updateSystemMenu(state);
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
		if (_title->isHidden()) {
			return false;
		}
		WINDOWPLACEMENT wp;
		wp.length = sizeof(WINDOWPLACEMENT);
		if (GetWindowPlacement(_handle, &wp)
			&& (wp.showCmd == SW_SHOWMAXIMIZED)) {
			const auto params = (LPNCCALCSIZE_PARAMS)lParam;
			const auto r = (wParam == TRUE)
				? &params->rgrc[0]
				: (LPRECT)lParam;
			const auto hMonitor = MonitorFromPoint(
				{ (r->left + r->right) / 2, (r->top + r->bottom) / 2 },
				MONITOR_DEFAULTTONEAREST);
			if (hMonitor) {
				MONITORINFO mi;
				mi.cbSize = sizeof(mi);
				if (GetMonitorInfo(hMonitor, &mi)) {
					*r = mi.rcWork;
					UINT uEdge = (UINT)-1;
					if (IsTaskbarAutoHidden(&mi.rcMonitor, &uEdge)) {
						switch (uEdge) {
						case ABE_LEFT: r->left += 1; break;
						case ABE_RIGHT: r->right -= 1; break;
						case ABE_TOP: r->top += 1; break;
						case ABE_BOTTOM: r->bottom -= 1; break;
						}
					}
				}
			}
		}
		if (result) *result = 0;
		return true;
	}

	case WM_NCRBUTTONUP: {
		if (_title->isHidden()) {
			return false;
		}
		SendMessage(_handle, WM_SYSCOMMAND, SC_MOUSEMENU, lParam);
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
		if (_shadow) {
			auto placement = WINDOWPLACEMENT{
				.length = sizeof(WINDOWPLACEMENT),
			};
			if (!GetWindowPlacement(_handle, &placement)) {
				LOG(("System Error: GetWindowPlacement failed."));
				return false;
			}
			_title->refreshAdditionalPaddings(_handle, placement);
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
			updateMargins();
			if (_shadow) {
				_title->refreshAdditionalPaddings(_handle);
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
		if (_shadow) {
			_title->refreshAdditionalPaddings(_handle);
			_shadow->update(WindowShadow::Change::Moved);
		}
	} return false;

	case WM_NCHITTEST: {
		if (!result || _title->isHidden()) {
			return false;
		}

		const auto p = MAKEPOINTS(lParam);
		auto r = RECT();
		GetWindowRect(_handle, &r);
		const auto mapped = QPoint(
			p.x - r.left + _marginsDelta.left(),
			p.y - r.top + _marginsDelta.top());
		*result = [&] {
			if (!window()->rect().contains(mapped)) {
				return HTTRANSPARENT;
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
			default: return HTTRANSPARENT;
			};
		}();
		_systemButtonOver.fire(systemButtonHitTest(*result));
	} return true;

	case WM_SYSCOMMAND: {
		if (wParam == SC_MOUSEMENU && !fixedSize()) {
			POINTS p = MAKEPOINTS(lParam);
			updateSystemMenu(window()->windowHandle()->windowState());
			TrackPopupMenu(
				_menu,
				TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
				p.x,
				p.y,
				0,
				_handle,
				0);
		}
	} return false;

	case WM_COMMAND: {
		if (HIWORD(wParam)) {
			return false;
		}
		const auto command = LOWORD(wParam);
		switch (command) {
		case SC_CLOSE:
			window()->close();
			return true;
		case SC_MINIMIZE:
			window()->setWindowState(
				window()->windowState() | Qt::WindowMinimized);
			return true;
		case SC_MAXIMIZE:
			if (!fixedSize()) {
				window()->setWindowState(Qt::WindowMaximized);
			}
			return true;
		case SC_RESTORE:
			window()->setWindowState(Qt::WindowNoState);
			return true;
		}
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

void WindowHelper::updateMargins() {
	if (_updatingMargins) return;

	_updatingMargins = true;
	const auto guard = gsl::finally([&] { _updatingMargins = false; });

	RECT r, a;

	GetClientRect(_handle, &r);
	a = r;

	const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
	const auto styleEx = GetWindowLongPtr(_handle, GWL_EXSTYLE);
	AdjustWindowRectEx(&a, style, false, styleEx);
	auto margins = QMargins(
		a.left - r.left,
		a.top - r.top,
		r.right - a.right,
		r.bottom - a.bottom);
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

void WindowHelper::updateSystemMenu() {
	updateSystemMenu(window()->windowHandle()->windowState());
}

void WindowHelper::updateSystemMenu(Qt::WindowState state) {
	if (!_menu) {
		return;
	}

	const auto menuToDisable = (state == Qt::WindowMaximized)
		? SC_MAXIMIZE
		: (state == Qt::WindowMinimized)
		? SC_MINIMIZE
		: SC_RESTORE;
	const auto itemCount = GetMenuItemCount(_menu);
	for (int i = 0; i < itemCount; ++i) {
		MENUITEMINFO itemInfo = { 0 };
		itemInfo.cbSize = sizeof(itemInfo);
		itemInfo.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
		if (!GetMenuItemInfo(_menu, i, TRUE, &itemInfo)) {
			break;
		}
		if (itemInfo.fType & MFT_SEPARATOR) {
			continue;
		} else if (!itemInfo.wID || itemInfo.wID == SC_CLOSE) {
			continue;
		}
		UINT fOldState = itemInfo.fState;
		UINT fState = itemInfo.fState & ~(MFS_DISABLED | MFS_DEFAULT);
		if (itemInfo.wID == menuToDisable
			|| (itemInfo.wID != SC_MINIMIZE
				&& itemInfo.wID != SC_MAXIMIZE
				&& itemInfo.wID != SC_RESTORE)) {
			fState |= MFS_DISABLED;
		}
		itemInfo.fMask = MIIM_STATE;
		itemInfo.fState = fState;
		if (!SetMenuItemInfo(_menu, i, TRUE, &itemInfo)) {
			break;
		}
	}
}

void WindowHelper::fixMaximizedWindow() {
	auto r = RECT();
	GetClientRect(_handle, &r);
	const auto style = GetWindowLongPtr(_handle, GWL_STYLE);
	const auto styleEx = GetWindowLongPtr(_handle, GWL_EXSTYLE);
	AdjustWindowRectEx(&r, style, false, styleEx);
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

} // namespace Platform
} // namespace Ui
