// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_window_title_win.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>

namespace Ui {
namespace Platform {

TitleWidget::TitleWidget(not_null<RpWidget*> parent)
: RpWidget(parent)
, _minimize(this, st::titleButtonMinimize)
, _maximizeRestore(this, st::titleButtonMaximize)
, _close(this, st::titleButtonClose)
, _shadow(this, st::titleShadow)
, _maximizedState(parent->windowState() & Qt::WindowMaximized)
, _activeState(parent->isActiveWindow()) {
	init();
}

void TitleWidget::setText(const QString &text) {
	window()->setWindowTitle(text);
}

not_null<RpWidget*> TitleWidget::window() const {
	return static_cast<RpWidget*>(parentWidget());
}

void TitleWidget::init() {
	_minimize->setClickedCallback([=] {
		window()->setWindowState(Qt::WindowMinimized);
		_minimize->clearState();
	});
	_minimize->setPointerCursor(false);
	_maximizeRestore->setClickedCallback([=] {
		window()->setWindowState(_maximizedState
			? Qt::WindowNoState
			: Qt::WindowMaximized);
		_maximizeRestore->clearState();
	});
	_maximizeRestore->setPointerCursor(false);
	_close->setClickedCallback([=] {
		window()->close();
		_close->clearState();
	});
	_close->setPointerCursor(false);

	setAttribute(Qt::WA_OpaquePaintEvent);

	window()->widthValue(
	) | rpl::start_with_next([=](int width) {
		setGeometry(0, 0, width, st::titleHeight);
	}, lifetime());

	window()->createWinId();
	connect(
		window()->windowHandle(),
		&QWindow::windowStateChanged,
		[=](Qt::WindowState state) { handleWindowStateChanged(state); });
	_activeState = isActiveWindow();
	updateButtonsState();
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	const auto active = isActiveWindow();
	if (_activeState != active) {
		_activeState = active;
		updateButtonsState();
	}
	QPainter(this).fillRect(e->rect(), active ? st::titleBgActive : st::titleBg);
}

void TitleWidget::updateControlsPosition() {
	auto right = 0;
	_close->moveToRight(right, 0); right += _close->width();
	_maximizeRestore->moveToRight(right, 0); right += _maximizeRestore->width();
	_minimize->moveToRight(right, 0);
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	updateControlsPosition();
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidget::updateControlsVisibility() {
	updateControlsPosition();
	update();
}

void TitleWidget::handleWindowStateChanged(Qt::WindowState state) {
	if (state == Qt::WindowMinimized) return;

	auto maximized = (state == Qt::WindowMaximized);
	if (_maximizedState != maximized) {
		_maximizedState = maximized;
		updateButtonsState();
	}
}

void TitleWidget::updateButtonsState() {
	const auto minimize = _activeState
		? &st::titleButtonMinimizeIconActive
		: nullptr;
	const auto minimizeOver = _activeState
		? &st::titleButtonMinimizeIconActiveOver
		: nullptr;
	_minimize->setIconOverride(minimize, minimizeOver);
	if (_maximizedState) {
		const auto restore = _activeState
			? &st::titleButtonRestoreIconActive
			: &st::titleButtonRestoreIcon;
		const auto restoreOver = _activeState
			? &st::titleButtonRestoreIconActiveOver
			: &st::titleButtonRestoreIconOver;
		_maximizeRestore->setIconOverride(restore, restoreOver);
	} else {
		const auto maximize = _activeState
			? &st::titleButtonMaximizeIconActive
			: nullptr;
		const auto maximizeOver = _activeState
			? &st::titleButtonMaximizeIconActiveOver
			: nullptr;
		_maximizeRestore->setIconOverride(maximize, maximizeOver);
	}
	const auto close = _activeState
		? &st::titleButtonCloseIconActive
		: nullptr;
	const auto closeOver = _activeState
		? &st::titleButtonCloseIconActiveOver
		: nullptr;
	_close->setIconOverride(close, closeOver);
}

HitTestResult TitleWidget::hitTest(QPoint point) const {
	if (false
		|| (_minimize->geometry().contains(point))
		|| (_maximizeRestore->geometry().contains(point))
		|| (_close->geometry().contains(point))
	) {
		return HitTestResult::SysButton;
	} else if (rect().contains(point)) {
		return HitTestResult::Caption;
	}
	return HitTestResult::None;
}

} // namespace Platform
} // namespace Ui
