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

TitleControls::TitleControls(
	not_null<RpWidget*> parent,
	const style::WindowTitle &st,
	Fn<void(bool maximized)> maximize)
: _st(&st)
, _minimize(parent, _st->minimize)
, _maximizeRestore(parent, _st->maximize)
, _close(parent, _st->close)
, _maximizedState(parent->windowState()
	& (Qt::WindowMaximized | Qt::WindowFullScreen))
, _activeState(parent->isActiveWindow()) {
	init(std::move(maximize));

	_close->paintRequest(
	) | rpl::start_with_next([=] {
		const auto active = window()->isActiveWindow();
		if (_activeState != active) {
			_activeState = active;
			updateButtonsState();
		}
	}, _close->lifetime());
}

void TitleControls::setStyle(const style::WindowTitle &st) {
	_st = &st;
	updateButtonsState();
}

not_null<const style::WindowTitle*> TitleControls::st() const {
	return _st;
}

QRect TitleControls::geometry() const {
	auto result = QRect();
	const auto add = [&](auto &&control) {
		if (!control->isHidden()) {
			result = result.united(control->geometry());
		}
	};
	add(_minimize);
	add(_maximizeRestore);
	add(_close);
	return result;
}

not_null<RpWidget*> TitleControls::parent() const {
	return static_cast<RpWidget*>(_close->parentWidget());
}

not_null<QWidget*> TitleControls::window() const {
	return _close->window();
}

void TitleControls::init(Fn<void(bool maximized)> maximize) {
	_minimize->setClickedCallback([=] {
		window()->setWindowState(
			window()->windowState() | Qt::WindowMinimized);
		_minimize->clearState();
	});
	_minimize->setPointerCursor(false);
	_maximizeRestore->setClickedCallback([=] {
		if (maximize) {
			maximize(!_maximizedState);
		} else {
			window()->setWindowState(_maximizedState
				? Qt::WindowNoState
				: Qt::WindowMaximized);
		}
		_maximizeRestore->clearState();
	});
	_maximizeRestore->setPointerCursor(false);
	_close->setClickedCallback([=] {
		window()->close();
		_close->clearState();
	});
	_close->setPointerCursor(false);

	parent()->widthValue(
	) | rpl::start_with_next([=](int width) {
		updateControlsPosition();
	}, _close->lifetime());

	window()->createWinId();
	QObject::connect(
		window()->windowHandle(),
		&QWindow::windowStateChanged,
		[=](Qt::WindowState state) { handleWindowStateChanged(state); });
	_activeState = parent()->isActiveWindow();
	updateButtonsState();
}

void TitleControls::setResizeEnabled(bool enabled) {
	_resizeEnabled = enabled;
	updateControlsVisibility();
}

void TitleControls::raise() {
	_minimize->raise();
	_maximizeRestore->raise();
	_close->raise();
}

void TitleControls::updateControlsPosition() {
	auto right = 0;
	_close->moveToRight(right, 0); right += _close->width();
	_maximizeRestore->moveToRight(right, 0);
	if (_resizeEnabled) {
		right += _maximizeRestore->width();
	}
	_minimize->moveToRight(right, 0);
}

void TitleControls::updateControlsVisibility() {
	_maximizeRestore->setVisible(_resizeEnabled);
	updateControlsPosition();
}

void TitleControls::handleWindowStateChanged(Qt::WindowState state) {
	if (state == Qt::WindowMinimized) {
		return;
	}

	auto maximized = (state == Qt::WindowMaximized)
		|| (state == Qt::WindowFullScreen);
	if (_maximizedState != maximized) {
		_maximizedState = maximized;
		updateButtonsState();
	}
}

void TitleControls::updateButtonsState() {
	const auto minimize = _activeState
		? &_st->minimizeIconActive
		: &_st->minimize.icon;
	const auto minimizeOver = _activeState
		? &_st->minimizeIconActiveOver
		: &_st->minimize.iconOver;
	_minimize->setIconOverride(minimize, minimizeOver);
	if (_maximizedState) {
		const auto restore = _activeState
			? &_st->restoreIconActive
			: &_st->restoreIcon;
		const auto restoreOver = _activeState
			? &_st->restoreIconActiveOver
			: &_st->restoreIconOver;
		_maximizeRestore->setIconOverride(restore, restoreOver);
	} else {
		const auto maximize = _activeState
			? &_st->maximizeIconActive
			: &_st->maximize.icon;
		const auto maximizeOver = _activeState
			? &_st->maximizeIconActiveOver
			: &_st->maximize.iconOver;
		_maximizeRestore->setIconOverride(maximize, maximizeOver);
	}
	const auto close = _activeState
		? &_st->closeIconActive
		: &_st->close.icon;
	const auto closeOver = _activeState
		? &_st->closeIconActiveOver
		: &_st->close.iconOver;
	_close->setIconOverride(close, closeOver);
}

TitleWidget::TitleWidget(not_null<RpWidget*> parent)
: RpWidget(parent)
, _controls(this, st::defaultWindowTitle)
, _shadow(this, st::titleShadow) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		setGeometry(0, 0, width, _controls.st()->height);
	}, lifetime());
}

void TitleWidget::setText(const QString &text) {
	window()->setWindowTitle(text);
}

void TitleWidget::setStyle(const style::WindowTitle &st) {
	_controls.setStyle(st);
	setGeometry(0, 0, window()->width(), _controls.st()->height);
	update();
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
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

HitTestResult TitleWidget::hitTest(QPoint point) const {
	if (_controls.geometry().contains(point)) {
		return HitTestResult::SysButton;
	} else if (rect().contains(point)) {
		return HitTestResult::Caption;
	}
	return HitTestResult::None;
}

} // namespace Platform
} // namespace Ui
