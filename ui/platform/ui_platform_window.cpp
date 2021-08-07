// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/ui_platform_window.h"

#include "ui/platform/ui_platform_window_title.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/rp_window.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QWindow>
#include <QtGui/QtEvents>

namespace Ui {
namespace Platform {
namespace {

[[nodiscard]] const style::Shadow &Shadow() {
	return st::callShadow;
}

} // namespace

BasicWindowHelper::BasicWindowHelper(not_null<RpWidget*> window)
: _window(window) {
	_window->setWindowFlag(Qt::Window);
}

not_null<RpWidget*> BasicWindowHelper::body() {
	return _window;
}

void BasicWindowHelper::setTitle(const QString &title) {
	_window->setWindowTitle(title);
}

void BasicWindowHelper::setTitleStyle(const style::WindowTitle &st) {
}

void BasicWindowHelper::setNativeFrame(bool enabled) {
}

void BasicWindowHelper::setMinimumSize(QSize size) {
	_window->setMinimumSize(size);
}

void BasicWindowHelper::setFixedSize(QSize size) {
	_window->setFixedSize(size);
}

void BasicWindowHelper::setStaysOnTop(bool enabled) {
	_window->setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
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
		} else if (e->type() == QEvent::MouseMove) {
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
		}
	}, body()->lifetime());
}

DefaultWindowHelper::DefaultWindowHelper(not_null<RpWidget*> window)
: BasicWindowHelper(window)
, _title(Ui::CreateChild<DefaultTitleWidget>(window.get()))
, _body(Ui::CreateChild<RpWidget>(window.get())) {
	init();
}

void DefaultWindowHelper::init() {
	_title->show();
	window()->setWindowFlag(Qt::FramelessWindowHint);

	if (WindowExtentsSupported()) {
		window()->setAttribute(Qt::WA_TranslucentBackground);
	}

	rpl::combine(
		window()->widthValue(),
		_windowState.value(),
		_title->shownValue()
	) | rpl::start_with_next([=](
			int width,
			Qt::WindowStates windowState,
			bool shown) {
		const auto area = resizeArea();
		_title->setGeometry(
			area.left(),
			area.top(),
			width - area.left() - area.right(),
			_title->st()->height);
	}, _title->lifetime());

	rpl::combine(
		window()->sizeValue(),
		_windowState.value(),
		_title->heightValue(),
		_title->shownValue()
	) | rpl::start_with_next([=](
			QSize size,
			Qt::WindowStates windowState,
			int titleHeight,
			bool titleShown) {
		const auto area = resizeArea();

		const auto sizeWithoutMargins = size
			.shrunkBy({ 0, titleShown ? titleHeight : 0, 0, 0 })
			.shrunkBy(area);

		const auto topLeft = QPoint(
			area.left(),
			area.top() + (titleShown ? titleHeight : 0));

		_body->setGeometry(QRect(topLeft, sizeWithoutMargins));
	}, _body->lifetime());

	window()->paintRequest(
	) | rpl::start_with_next([=] {
		const auto area = resizeArea();

		if (area.isNull()) {
			return;
		}

		Painter p(window());

		if (hasShadow()) {
			Ui::Shadow::paint(
				p,
				QRect(QPoint(), window()->size()).marginsRemoved(area),
				window()->width(),
				Shadow());
		} else {
			paintBorders(p);
		}
	}, window()->lifetime());

	rpl::combine(
		window()->shownValue(),
		_windowState.value()
	) | rpl::start_with_next([=](bool shown, Qt::WindowStates windowState) {
		if (shown) {
			updateWindowExtents();
		}
	}, window()->lifetime());

	window()->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e.get());
			const auto currentPoint = mouseEvent->windowPos().toPoint();
			const auto edges = edgesFromPos(currentPoint);

			if (mouseEvent->button() == Qt::LeftButton && edges) {
				window()->windowHandle()->startSystemResize(edges);
			}
		} else if (e->type() == QEvent::WindowStateChange) {
			_windowState = window()->windowState();
		}
	}, window()->lifetime());

	QCoreApplication::instance()->installEventFilter(this);
}

not_null<RpWidget*> DefaultWindowHelper::body() {
	return _body;
}

bool DefaultWindowHelper::hasShadow() const {
	const auto center = window()->geometry().center();
	return WindowExtentsSupported() && TranslucentWindowsSupported(center);
}

QMargins DefaultWindowHelper::resizeArea() const {
	if (window()->isMaximized()
		|| window()->isFullScreen()
		|| _title->isHidden()) {
		return QMargins();
	}

	return Shadow().extend;
}

Qt::Edges DefaultWindowHelper::edgesFromPos(const QPoint &pos) const {
	const auto area = resizeArea();
	const auto ignoreHorizontal = (window()->minimumWidth()
		== window()->maximumWidth());
	const auto ignoreVertical = (window()->minimumHeight()
		== window()->maximumHeight());

	if (area.isNull()) {
		return Qt::Edges();
	} else if (!ignoreHorizontal && pos.x() <= area.left()) {
		if (!ignoreVertical && pos.y() <= area.top()) {
			return Qt::LeftEdge | Qt::TopEdge;
		} else if (!ignoreVertical
			&& pos.y() >= (window()->height() - area.bottom())) {
			return Qt::LeftEdge | Qt::BottomEdge;
		}

		return Qt::LeftEdge;
	} else if (!ignoreHorizontal
		&& pos.x() >= (window()->width() - area.right())) {
		if (!ignoreVertical && pos.y() <= area.top()) {
			return Qt::RightEdge | Qt::TopEdge;
		} else if (!ignoreVertical
			&& pos.y() >= (window()->height() - area.bottom())) {
			return Qt::RightEdge | Qt::BottomEdge;
		}

		return Qt::RightEdge;
	} else if (!ignoreVertical && pos.y() <= area.top()) {
		return Qt::TopEdge;
	} else if (!ignoreVertical
		&& pos.y() >= (window()->height() - area.bottom())) {
		return Qt::BottomEdge;
	}

	return Qt::Edges();
}

bool DefaultWindowHelper::eventFilter(QObject *obj, QEvent *e) {
	// doesn't work with RpWidget::events() for some reason
	if (e->type() == QEvent::MouseMove
		&& obj->isWidgetType()
		&& window()->isAncestorOf(static_cast<QWidget*>(obj))) {
		const auto mouseEvent = static_cast<QMouseEvent*>(e);
		const auto currentPoint = mouseEvent->windowPos().toPoint();
		const auto edges = edgesFromPos(currentPoint);

		if (mouseEvent->buttons() == Qt::NoButton) {
			updateCursor(edges);
		}
	}

	return QObject::eventFilter(obj, e);
}

void DefaultWindowHelper::setTitle(const QString &title) {
	_title->setText(title);
	window()->setWindowTitle(title);
}

void DefaultWindowHelper::setTitleStyle(const style::WindowTitle &st) {
	const auto area = resizeArea();
	_title->setStyle(st);
	_title->setGeometry(
		area.left(),
		area.top(),
		window()->width() - area.left() - area.right(),
		_title->st()->height);
}

void DefaultWindowHelper::setNativeFrame(bool enabled) {
	window()->windowHandle()->setFlag(Qt::FramelessWindowHint, !enabled);
	_title->setVisible(!enabled);
	updateWindowExtents();
}

void DefaultWindowHelper::setMinimumSize(QSize size) {
	const auto sizeWithMargins = size.grownBy(bodyPadding());
	window()->setMinimumSize(sizeWithMargins);
}

void DefaultWindowHelper::setFixedSize(QSize size) {
	const auto sizeWithMargins = size.grownBy(bodyPadding());
	window()->setFixedSize(sizeWithMargins);
	_title->setResizeEnabled(false);
}

void DefaultWindowHelper::setGeometry(QRect rect) {
	window()->setGeometry(rect.marginsAdded(bodyPadding()));
}

void DefaultWindowHelper::paintBorders(QPainter &p) {
	const auto titleBackground = window()->isActiveWindow()
		? _title->st()->bgActive
		: _title->st()->bg;

	const auto defaultTitleBackground = window()->isActiveWindow()
		? st::defaultWindowTitle.bgActive
		: st::defaultWindowTitle.bg;

	const auto borderColor = QBrush(titleBackground).isOpaque()
		? titleBackground
		: defaultTitleBackground;

	const auto area = resizeArea();

	p.fillRect(
		0,
		area.top(),
		area.left(),
		window()->height() - area.top() - area.bottom(),
		borderColor);

	p.fillRect(
		window()->width() - area.right(),
		area.top(),
		area.right(),
		window()->height() - area.top() - area.bottom(),
		borderColor);

	p.fillRect(
		0,
		0,
		window()->width(),
		area.top(),
		borderColor);

	p.fillRect(
		0,
		window()->height() - area.bottom(),
		window()->width(),
		area.bottom(),
		borderColor);
}

void DefaultWindowHelper::updateWindowExtents() {
	if (hasShadow() && !_title->isHidden()) {
		Platform::SetWindowExtents(
			window()->windowHandle(),
			resizeArea());

		_extentsSet = true;
	} else if (_extentsSet) {
		Platform::UnsetWindowExtents(window()->windowHandle());
		_extentsSet = false;
	}
}

int DefaultWindowHelper::titleHeight() const {
	return _title->isHidden() ? 0 : _title->height();
}

QMargins DefaultWindowHelper::bodyPadding() const {
	return resizeArea() + QMargins{ 0, titleHeight(), 0, 0 };
}

void DefaultWindowHelper::updateCursor(Qt::Edges edges) {
	if (((edges & Qt::LeftEdge) && (edges & Qt::TopEdge))
		|| ((edges & Qt::RightEdge) && (edges & Qt::BottomEdge))) {
		window()->setCursor(QCursor(Qt::SizeFDiagCursor));
	} else if (((edges & Qt::LeftEdge) && (edges & Qt::BottomEdge))
		|| ((edges & Qt::RightEdge) && (edges & Qt::TopEdge))) {
		window()->setCursor(QCursor(Qt::SizeBDiagCursor));
	} else if ((edges & Qt::LeftEdge) || (edges & Qt::RightEdge)) {
		window()->setCursor(QCursor(Qt::SizeHorCursor));
	} else if ((edges & Qt::TopEdge) || (edges & Qt::BottomEdge)) {
		window()->setCursor(QCursor(Qt::SizeVerCursor));
	} else {
		window()->setCursor(QCursor(Qt::ArrowCursor));
	}
}

} // namespace Platform
} // namespace Ui
