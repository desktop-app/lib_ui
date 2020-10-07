// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/ui_platform_window.h"

#include "ui/widgets/window.h"

#include <QtGui/QWindow>
#include <QtGui/QtEvents>

namespace Ui {
namespace Platform {

BasicWindowHelper::BasicWindowHelper(not_null<RpWidget*> window)
: _window(window) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
	_window->setWindowFlag(Qt::Window);
#else // Qt >= 5.9
	_window->setWindowFlags(_window->windowFlags() | Qt::Window);
#endif // Qt >= 5.9
}

not_null<RpWidget*> BasicWindowHelper::body() {
	return _window;
}

void BasicWindowHelper::setTitle(const QString &title) {
	_window->setWindowTitle(title);
}

void BasicWindowHelper::setTitleStyle(const style::WindowTitle &st) {
}

void BasicWindowHelper::setMinimumSize(QSize size) {
	_window->setMinimumSize(size);
}

void BasicWindowHelper::setFixedSize(QSize size) {
	_window->setFixedSize(size);
}

void BasicWindowHelper::setGeometry(QRect rect) {
	_window->setGeometry(rect);
}

void BasicWindowHelper::showFullScreen() {
	_window->showFullScreen();
}

void BasicWindowHelper::showNormal() {
	_window->showNormal();
}

void BasicWindowHelper::close() {
	_window->close();
}

void BasicWindowHelper::setBodyTitleArea(
		Fn<WindowTitleHitTestFlags(QPoint)> testMethod) {
	Expects(!_bodyTitleAreaTestMethod);

	if (!testMethod) {
		return;
	}
	_bodyTitleAreaTestMethod = std::move(testMethod);
	setupBodyTitleAreaEvents();
}

void BasicWindowHelper::setupBodyTitleAreaEvents() {
	// This is not done on macOS, because startSystemMove
	// doesn't work from event handler there.
	body()->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto hitTest = [&] {
			return bodyTitleAreaHit(
				static_cast<QMouseEvent*>(e.get())->pos());
		};
		if (e->type() == QEvent::MouseButtonDblClick) {
			_mousePressed = false;
			const auto hit = hitTest();
			if (hit & WindowTitleHitTestFlag::Maximize) {
				const auto state = _window->windowState();
				if (state & Qt::WindowMaximized) {
					_window->setWindowState(state & ~Qt::WindowMaximized);
				} else {
					_window->setWindowState(state | Qt::WindowMaximized);
				}
			} else if (hit & WindowTitleHitTestFlag::FullScreen) {
				if (_window->isFullScreen()) {
					showNormal();
				} else {
					showFullScreen();
				}
			}
		} else if (e->type() == QEvent::MouseButtonRelease) {
			_mousePressed = false;
		} else if (e->type() == QEvent::MouseButtonPress
			&& (static_cast<QMouseEvent*>(e.get())->button()
				== Qt::LeftButton)) {
			_mousePressed = true;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
		} else if (e->type() == QEvent::MouseMove) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e.get());
			if (_mousePressed
#ifndef Q_OS_WIN // We handle fullscreen startSystemMove() only on Windows.
				&& !_window->isFullScreen()
#endif // !Q_OS_WIN
				&& (hitTest() & WindowTitleHitTestFlag::Move)) {

#ifdef Q_OS_WIN
				if (_window->isFullScreen()) {
					// On Windows we just jump out of fullscreen
					// like we do automatically for dragging a window
					// by title bar in a maximized state.
					showNormal();
				}
#endif // Q_OS_WIN
				_mousePressed = false;
				_window->windowHandle()->startSystemMove();
			}
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
		}
	}, body()->lifetime());
}

} // namespace Platform
} // namespace Ui
