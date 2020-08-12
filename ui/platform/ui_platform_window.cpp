// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/ui_platform_window.h"

#include "ui/rp_widget.h"

#include <QtGui/QWindow>
#include <QtGui/QtEvents>

namespace Ui {
namespace Platform {

BasicWindowHelper::BasicWindowHelper(not_null<RpWidget*> window)
: _window(window) {
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

void BasicWindowHelper::setBodyTitleArea(Fn<bool(QPoint)> testMethod) {
	Expects(!_bodyTitleAreaTestMethod);

	if (!testMethod) {
		return;
	}
	_bodyTitleAreaTestMethod = std::move(testMethod);
	if (customBodyTitleAreaHandling()) {
		return;
	}
	body()->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonDblClick) {
			if (bodyTitleAreaHit(static_cast<QMouseEvent*>(e.get())->pos())) {
				const auto state = _window->windowState();
				if (state & Qt::WindowMaximized) {
					_window->setWindowState(state & ~Qt::WindowMaximized);
				} else {
					_window->setWindowState(state | Qt::WindowMaximized);
				}
			}
		} else if (e->type() == QEvent::MouseMove) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e.get());
			if (bodyTitleAreaHit(mouseEvent->pos())
				&& (mouseEvent->buttons() & Qt::LeftButton)) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
				_window->windowHandle()->startSystemMove();
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
			}
		}
	}, body()->lifetime());
}

} // namespace Platform
} // namespace Ui
