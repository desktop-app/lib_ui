// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_window_title_win.h"

#include "ui/platform/win/ui_window_win.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/rp_window.h"
#include "ui/ui_utility.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/win/base_windows_safe_library.h"
#include "base/debug_log.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>

#include <windows.h>
#include <shellscalingapi.h>

namespace Ui {
namespace Platform {
namespace {

HRESULT(__stdcall *GetScaleFactorForMonitor)(
	_In_ HMONITOR hMon,
	_Out_ DEVICE_SCALE_FACTOR *pScale);

[[nodiscard]] bool ScaleQuerySupported() {
	static const auto Result = [&] {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto shcore = base::Platform::SafeLoadLibrary(L"Shcore.dll");
		return LOAD_SYMBOL(shcore, GetScaleFactorForMonitor);
#undef LOAD_SYMBOL
	}();
	return Result;
}

} // namespace

namespace internal {

TitleControls::Layout TitleControlsLayout() {
	return TitleControls::Layout{
		.right = {
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
			TitleControls::Control::Close,
		}
	};
}

} // namespace internal

struct TitleWidget::PaddingHelper {
	explicit PaddingHelper(QWidget *parent) : controlsParent(parent) {
	}

	RpWidget controlsParent;
	rpl::variable<int> padding = 0;
};

TitleWidget::TitleWidget(not_null<RpWidget*> parent)
: RpWidget(parent)
, _paddingHelper(CheckTitlePaddingRequired()
	? std::make_unique<PaddingHelper>(this)
	: nullptr)
, _controls(
	_paddingHelper ? &_paddingHelper->controlsParent : this,
	st::defaultWindowTitle)
, _shadow(this, st::titleShadow) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		refreshGeometryWithWidth(width);
	}, lifetime());
}

void TitleWidget::initInWindow(not_null<RpWindow*> window) {
	window->hitTestRequests(
	) | rpl::filter([=](not_null<HitTestRequest*> request) {
		return !isHidden() && geometry().contains(request->point);
	}) | rpl::start_with_next([=](not_null<HitTestRequest*> request) {
		request->result = hitTest(request->point);
	}, lifetime());

	SetupSemiNativeSystemButtons(&_controls, window, lifetime(), [=] {
		return !isHidden() && (_controls.st()->height > 0);
	});
}

TitleWidget::~TitleWidget() = default;

void TitleWidget::setText(const QString &text) {
	window()->setWindowTitle(text);
}

void TitleWidget::setStyle(const style::WindowTitle &st) {
	_controls.setStyle(st);
	if (!st.shadow) {
		_shadow.destroy();
	} else if (!_shadow) {
		_shadow.create(this, st::titleShadow);
		updateShadowGeometry();
	}
	refreshGeometryWithWidth(window()->width());
}

void TitleWidget::updateShadowGeometry() {
	const auto thickness = st::lineWidth;
	_shadow->setGeometry(0, height() - thickness, width(), thickness);
}

void TitleWidget::refreshGeometryWithWidth(int width) {
	const auto add = additionalPadding();
	setGeometry(0, 0, width, _controls.st()->height + add);
	if (_paddingHelper) {
		_paddingHelper->controlsParent.setGeometry(
			0,
			add,
			width,
			_controls.st()->height);
	}
	update();
}

not_null<const style::WindowTitle*> TitleWidget::st() const {
	return _controls.st();
}

void TitleWidget::setResizeEnabled(bool enabled) {
	_controls.setResizeEnabled(enabled);
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	const auto active = window()->isActiveWindow();
	QPainter(this).fillRect(
		e->rect(),
		active ? _controls.st()->bgActive : _controls.st()->bg);
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	if (_shadow) {
		updateShadowGeometry();
	}
}

HitTestResult TitleWidget::hitTest(QPoint point) const {
	const auto controlsResult = _controls.hitTest(point);
	return (controlsResult != HitTestResult::None)
		? controlsResult
		: HitTestResult::Caption;
}

bool TitleWidget::additionalPaddingRequired() const {
	return _paddingHelper && !isHidden();
}

void TitleWidget::refreshAdditionalPaddings() {
	if (!additionalPaddingRequired()) {
		return;
	} else if (const auto handle = GetCurrentHandle(this)) {
		refreshAdditionalPaddings(handle);
	}
}

void TitleWidget::refreshAdditionalPaddings(HWND handle) {
	if (!additionalPaddingRequired()) {
		return;
	}
	auto placement = WINDOWPLACEMENT{
		.length = sizeof(WINDOWPLACEMENT),
	};
	if (!GetWindowPlacement(handle, &placement)) {
		LOG(("System Error: GetWindowPlacement failed."));
		return;
	}
	refreshAdditionalPaddings(handle, placement);
}

void TitleWidget::refreshAdditionalPaddings(
		HWND handle,
		const WINDOWPLACEMENT &placement) {
	if (!additionalPaddingRequired()) {
		return;
	}
	auto geometry = RECT();
	if (!GetWindowRect(handle, &geometry)) {
		LOG(("System Error: GetWindowRect failed."));
		return;
	}
	const auto normal = placement.rcNormalPosition;
	const auto rounded = (normal.left == geometry.left)
		&& (normal.right == geometry.right)
		&& (normal.top == geometry.top)
		&& (normal.bottom == geometry.bottom);
	const auto padding = [&] {
		if (!rounded) {
			return 0;
		}
		const auto monitor = MonitorFromWindow(
			handle,
			MONITOR_DEFAULTTONEAREST);
		if (!monitor) {
			LOG(("System Error: MonitorFromWindow failed."));
			return -1;
		}
		auto factor = DEVICE_SCALE_FACTOR();
		if (!SUCCEEDED(GetScaleFactorForMonitor(monitor, &factor))) {
			LOG(("System Error: GetScaleFactorForMonitor failed."));
			return -1;
		} else if (factor < 100 || factor > 500) {
			LOG(("System Error: Bad scale factor %1.").arg(int(factor)));
			return -1;
		}
		const auto pixels = (factor + 50) / 100;
		return int(base::SafeRound(
			pixels / window()->windowHandle()->devicePixelRatio()));
	}();
	if (padding < 0) {
		return;
	}
	setAdditionalPadding(padding);
}

int TitleWidget::additionalPadding() const {
	return _paddingHelper ? _paddingHelper->padding.current() : 0;
}

rpl::producer<int> TitleWidget::additionalPaddingValue() const {
	return _paddingHelper ? _paddingHelper->padding.value() : rpl::single(0);
}

void TitleWidget::setAdditionalPadding(int padding) {
	Expects(_paddingHelper != nullptr);

	padding /= window()->devicePixelRatio();
	if (_paddingHelper->padding.current() == padding) {
		return;
	}
	_paddingHelper->padding = padding;
	refreshGeometryWithWidth(window()->width());
}

void TitleWidget::setVisibleHook(bool visible) {
	RpWidget::setVisibleHook(visible);
	if (additionalPaddingRequired()) {
		PostponeCall(this, [=] {
			refreshAdditionalPaddings();
		});
	}
}

bool CheckTitlePaddingRequired() {
	return ::Platform::IsWindows11OrGreater() && ScaleQuerySupported();
}

} // namespace Platform
} // namespace Ui
