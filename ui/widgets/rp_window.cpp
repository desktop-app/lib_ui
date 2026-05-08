// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/rp_window.h"

#include "ui/platform/ui_platform_window.h"
#include "ui/style/style_runtime.h"
#include "ui/style/style_core_scale.h"

#include <QtGui/QScreen>

namespace Ui {
namespace {

// Compute the ScaleKey for this window from its current screen. Scale
// component is the global user-set interface multiplier (today's
// `style::Scale()`); DPR component is the screen's rounded device pixel
// ratio. Phase 6d will refine the dpr part with native-API queries to catch
// sub-rounded changes.
[[nodiscard]] style::ScaleKey ScaleKeyFor(const QWidget *window) {
	const auto screen = window->screen();
	const auto dpr = screen
		? std::max(int(std::round(screen->devicePixelRatio())), 1)
		: 1;
	return style::MakeScaleKey(style::Scale(), dpr);
}

} // namespace

RpWindow::RpWindow(QWidget *parent)
: RpWidget(parent)
, _helper(Platform::CreateWindowHelper(this)) {
	Expects(_helper != nullptr);

	_helper->initInWindow(this);

	if (style::RuntimeScaleEnabled()) {
		style::AttachContextToWindow(this, ScaleKeyFor(this));

		// Refresh ScaleKey on Qt-reported screen / DPR transitions.
		// ScreenChangeInternal fires when the window moves to another
		// monitor; DevicePixelRatioChange (Qt 6+) fires when the same
		// monitor's reported DPR changes. Sub-rounded changes that don't
		// cross Qt's rounding threshold (e.g. Windows 225%->250% with
		// default policy) need native event hooks — Phase 6d.
		events(
		) | rpl::on_next([this](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::ScreenChangeInternal
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
				|| type == QEvent::DevicePixelRatioChange
#endif
			) {
				style::UpdateWindowScaleKey(this, ScaleKeyFor(this));
				update();
			}
		}, lifetime());
	}

	hide();
}

RpWindow::~RpWindow() = default;

not_null<RpWidget*> RpWindow::body() {
	return _helper->body();
}

not_null<const RpWidget*> RpWindow::body() const {
	return _helper->body().get();
}

QMargins RpWindow::frameMargins() const {
	return _helper->frameMargins();
}

int RpWindow::additionalContentPadding() const {
	return _helper->additionalContentPadding();
}

rpl::producer<int> RpWindow::additionalContentPaddingValue() const {
	return _helper->additionalContentPaddingValue();
}

auto RpWindow::hitTestRequests() const
-> rpl::producer<not_null<Platform::HitTestRequest*>> {
	return _helper->hitTestRequests();
}

rpl::producer<Platform::HitTestResult> RpWindow::systemButtonOver() const {
	return _helper->systemButtonOver();
}

rpl::producer<Platform::HitTestResult> RpWindow::systemButtonDown() const {
	return _helper->systemButtonDown();
}

void RpWindow::overrideSystemButtonOver(Platform::HitTestResult button) {
	_helper->overrideSystemButtonOver(button);
}

void RpWindow::overrideSystemButtonDown(Platform::HitTestResult button) {
	_helper->overrideSystemButtonDown(button);
}

void RpWindow::setTitle(const QString &title) {
	_helper->setTitle(title);
}

void RpWindow::setTitleStyle(const style::WindowTitle &st) {
	_helper->setTitleStyle(st);
}

void RpWindow::setNativeFrame(bool enabled) {
	_helper->setNativeFrame(enabled);
}

void RpWindow::setMinimumSize(QSize size) {
	_helper->setMinimumSize(size);
}

void RpWindow::setFixedSize(QSize size) {
	_helper->setFixedSize(size);
}

void RpWindow::setStaysOnTop(bool enabled) {
	_helper->setStaysOnTop(enabled);
}

void RpWindow::setGeometry(QRect rect) {
	_helper->setGeometry(rect);
}

void RpWindow::showFullScreen() {
	_helper->showFullScreen();
}

void RpWindow::showNormal() {
	_helper->showNormal();
}

void RpWindow::close() {
	_helper->close();
}

void RpWindow::setBodyTitleArea(
		Fn<WindowTitleHitTestFlags(QPoint)> testMethod) {
	_helper->setBodyTitleArea(std::move(testMethod));
}

bool RpWindow::mousePressCancelled() const {
	return _helper->mousePressCancelled();
}

int RpWindow::manualRoundingRadius() const {
	return _helper->manualRoundingRadius();
}

const style::TextStyle &RpWindow::titleTextStyle() const {
	return _helper->titleTextStyle();
}

} // namespace Ui
