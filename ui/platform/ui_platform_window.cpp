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
#include "styles/palette.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QWindow>
#include <QtGui/QtEvents>

namespace Ui {
namespace Platform {
namespace {

[[nodiscard]] const style::Shadow &Shadow() {
	return st::callShadow;
}

[[nodiscard]] int Radius() {
	return st::callRadius;
}

[[nodiscard]] std::array<QImage, 4> PrepareSides(
		const style::Shadow &shadow) {
	auto result = std::array<QImage, 4>();
	const auto extend = shadow.extend;
	const auto make = [&](
			int index,
			const style::icon &icon,
			auto &&postprocess) {
		result[index] = icon.instance(st::windowShadowFg->c);
		auto p = QPainter(&result[index]);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		postprocess(p, icon.width(), icon.height());
	};
	make(0, shadow.left, [&](QPainter &p, int width, int height) {
		const auto skip = extend.left();
		p.fillRect(skip, 0, width - skip, height, Qt::transparent);
	});
	make(1, shadow.top, [&](QPainter &p, int width, int height) {
		const auto skip = extend.top();
		p.fillRect(0, skip, width, height - skip, Qt::transparent);
	});
	make(2, shadow.right, [&](QPainter &p, int width, int height) {
		const auto skip = extend.right();
		p.fillRect(0, 0, width - skip, height, Qt::transparent);
	});
	make(3, shadow.bottom, [&](QPainter &p, int width, int height) {
		const auto skip = extend.bottom();
		p.fillRect(0, 0, width, height - skip, Qt::transparent);
	});
	return result;
}

[[nodiscard]] std::array<QImage, 4> PrepareCorners(
		const style::Shadow &shadow,
		int radius) {
	auto result = std::array<QImage, 4>();
	const auto extend = shadow.extend;
	const auto make = [&](
			int index,
			const style::icon &icon,
			auto &&postprocess) {
		result[index] = icon.instance(st::windowShadowFg->c);
		auto p = QPainter(&result[index]);
		auto hq = PainterHighQualityEnabler(p);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(Qt::transparent);
		p.setPen(Qt::NoPen);
		postprocess(p, icon.width(), icon.height());
	};
	make(0, shadow.topLeft, [&](QPainter &p, int width, int height) {
		const auto skipx = extend.left();
		const auto skipy = extend.top();
		width += 2 * radius;
		height += 2 * radius;
		p.drawRoundedRect(skipx, skipy, width, height, radius, radius);
	});
	make(1, shadow.bottomLeft, [&](QPainter &p, int width, int height) {
		const auto skipx = extend.left();
		const auto skipy = extend.bottom() + 2 * radius;
		width += 2 * radius;
		height += 2 * radius;
		p.drawRoundedRect(skipx, -skipy, width, height, radius, radius);
	});
	make(2, shadow.topRight, [&](QPainter &p, int width, int height) {
		const auto skipx = extend.right() + 2 * radius;
		const auto skipy = extend.top();
		width += 2 * radius;
		height += 2 * radius;
		p.drawRoundedRect(-skipx, skipy, width, height, radius, radius);
	});
	make(3, shadow.bottomRight, [&](QPainter &p, int width, int height) {
		const auto skipx = extend.right() + 2 * radius;
		const auto skipy = extend.bottom() + 2 * radius;
		width += 2 * radius;
		height += 2 * radius;
		p.drawRoundedRect(-skipx, -skipy, width, height, radius, radius);
	});
	return result;
}

} // namespace

BasicWindowHelper::BasicWindowHelper(not_null<RpWidget*> window)
: _window(window) {
	_window->setWindowFlag(Qt::Window);
}

void BasicWindowHelper::initInWindow(not_null<RpWindow*> window) {
}

not_null<RpWidget*> BasicWindowHelper::body() {
	return _window;
}

QMargins BasicWindowHelper::frameMargins() {
	return nativeFrameMargins();
}

int BasicWindowHelper::additionalContentPadding() const {
	return 0;
}

rpl::producer<int> BasicWindowHelper::additionalContentPaddingValue() const {
	return rpl::single(0);
}

auto BasicWindowHelper::hitTestRequests() const
-> rpl::producer<not_null<HitTestRequest*>> {
	return rpl::never<not_null<HitTestRequest*>>();
}

rpl::producer<HitTestResult> BasicWindowHelper::systemButtonOver() const {
	return rpl::never<HitTestResult>();
}

rpl::producer<HitTestResult> BasicWindowHelper::systemButtonDown() const {
	return rpl::never<HitTestResult>();
}

void BasicWindowHelper::overrideSystemButtonOver(HitTestResult button) {
	Expects(button == HitTestResult::None);
}

void BasicWindowHelper::overrideSystemButtonDown(HitTestResult button) {
	Expects(button == HitTestResult::None);
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

int BasicWindowHelper::manualRoundingRadius() const {
	return 0;
}

void BasicWindowHelper::setBodyTitleArea(
		Fn<WindowTitleHitTestFlags(QPoint)> testMethod) {
	Expects(!_bodyTitleAreaTestMethod || testMethod);

	if (!testMethod) {
		return;
	}
	if (!_bodyTitleAreaTestMethod) {
		setupBodyTitleAreaEvents();
	}
	_bodyTitleAreaTestMethod = std::move(testMethod);
}

const style::TextStyle &BasicWindowHelper::titleTextStyle() const {
	return st::defaultWindowTitle.style;
}

QMargins BasicWindowHelper::nativeFrameMargins() const {
	const auto inner = window()->geometry();
	const auto outer = window()->frameGeometry();
	return QMargins(
		inner.x() - outer.x(),
		inner.y() - outer.y(),
		outer.x() + outer.width() - inner.x() - inner.width(),
		outer.y() + outer.height() - inner.y() - inner.height());
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
		} else if (e->type() == QEvent::MouseButtonPress) {
			const auto ee = static_cast<QMouseEvent*>(e.get());
			if (ee->button() == Qt::LeftButton) {
				_mousePressed = true;
			} else if (ee->button() == Qt::RightButton) {
				if (hitTest() & WindowTitleHitTestFlag::Menu) {
					ShowWindowMenu(window(), ee->windowPos().toPoint());
				}
			}
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
				SendSynteticMouseEvent(
					body().get(),
					QEvent::MouseButtonRelease,
					Qt::LeftButton);
			}
		}
	}, body()->lifetime());
}

DefaultWindowHelper::DefaultWindowHelper(not_null<RpWidget*> window)
: BasicWindowHelper(window)
, _title(Ui::CreateChild<DefaultTitleWidget>(window.get()))
, _body(Ui::CreateChild<RpWidget>(window.get()))
, _roundRect(Radius(), st::windowBg)
, _sides(PrepareSides(Shadow()))
, _corners(PrepareCorners(Shadow(), Radius())) {
	init();
}

void DefaultWindowHelper::init() {
	if (WindowMarginsSupported()) {
		window()->setAttribute(Qt::WA_TranslucentBackground);
	}

	_title->show();

	rpl::combine(
		window()->shownValue(),
		_title->shownValue(),
		_windowState.value()
	) | rpl::filter([=](
			bool shown,
			bool titleShown,
			Qt::WindowStates windowState) {
		return shown;
	}) | rpl::start_with_next([=](
			bool shown,
			bool titleShown,
			Qt::WindowStates windowState) {
		_lastGeometry = _body->mapToGlobal(_body->rect());
		window()->windowHandle()->setFlag(Qt::FramelessWindowHint, titleShown);
		updateWindowMargins();
		if (_fixedSize) {
			setFixedSize(*_fixedSize);
		} else if (_minimumSize) {
			setMinimumSize(*_minimumSize);
		}
	}, window()->lifetime());

	_title->shownValue(
	) | rpl::filter([=] {
		return !window()->isHidden()
			&& !window()->isMaximized()
			&& !window()->isFullScreen();
	}) | rpl::start_with_next([=] {
		setGeometry(_lastGeometry);
	}, window()->lifetime());

	rpl::combine(
		window()->widthValue(),
		_windowState.value(),
		_title->shownValue(),
		TitleControlsLayoutValue()
	) | rpl::start_with_next([=](
			int width,
			Qt::WindowStates windowState,
			bool shown,
			TitleControls::Layout controlsLayout) {
		const auto area = resizeArea();
		_title->setGeometry(
			area.left(),
			area.top(),
			width - area.left() - area.right(),
			_title->controlsGeometry().height()
				? _title->st()->height
				: 0);
	}, _title->lifetime());

	rpl::combine(
		window()->sizeValue(),
		_windowState.value(),
		_title->heightValue(),
		_title->shownValue(),
		TitleControlsLayoutValue()
	) | rpl::start_with_next([=](
			QSize size,
			Qt::WindowStates windowState,
			int titleHeight,
			bool titleShown,
			TitleControls::Layout controlsLayout) {
		const auto area = resizeArea();

		const auto sizeWithoutMargins = size
			.shrunkBy({ 0, titleShown ? titleHeight : 0, 0, 0 })
			.shrunkBy(area);

		const auto topLeft = QPoint(
			area.left(),
			area.top() + (titleShown ? titleHeight : 0));

		_body->setGeometry(QRect(topLeft, sizeWithoutMargins));
		updateRoundingOverlay();
	}, _body->lifetime());

	window()->paintRequest(
	) | rpl::filter([=] {
		return !hasShadow() && !resizeArea().isNull();
	}) | rpl::start_with_next([=] {
		Painter p(window());
		paintBorders(p);
	}, window()->lifetime());

	window()->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			const auto mouseEvent = static_cast<QMouseEvent*>(e.get());
			const auto currentPoint = mouseEvent->windowPos().toPoint();
			const auto edges = edgesFromPos(currentPoint);

			if (mouseEvent->button() == Qt::LeftButton && edges) {
				window()->windowHandle()->startSystemResize(edges);
				SendSynteticMouseEvent(
					window().get(),
					QEvent::MouseButtonRelease,
					Qt::LeftButton);
			}
		} else if (e->type() == QEvent::WindowStateChange) {
			_windowState = window()->windowState();
		}
	}, window()->lifetime());

	QCoreApplication::instance()->installEventFilter(this);
}

void DefaultWindowHelper::updateRoundingOverlay() {
	if (!hasShadow() || resizeArea().isNull()) {
		_roundingOverlay.destroy();
		return;
	} else if (_roundingOverlay) {
		return;
	}

	_roundingOverlay.create(window());
	_roundingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	_roundingOverlay->show();

	window()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_roundingOverlay->setGeometry(QRect(QPoint(), size));
	}, _roundingOverlay->lifetime());

	_roundingOverlay->paintRequest(
	) | rpl::filter([=](QRect clip) {
		const auto rect = window()->rect().marginsRemoved(resizeArea());
		const auto radius = Radius();
		const auto radiusWithFix = radius - 1;
		const auto radiusSize = QSize(radius, radius);
		return clip.intersects(QRect(
				rect.topLeft(),
				radiusSize
			)) || clip.intersects(QRect(
				rect.topRight() - QPoint(radiusWithFix, 0),
				radiusSize
			)) || clip.intersects(QRect(
				rect.bottomLeft() - QPoint(0, radiusWithFix),
				radiusSize
			)) || clip.intersects(QRect(
				rect.bottomRight() - QPoint(radiusWithFix, radiusWithFix),
				radiusSize
			)) || !rect.contains(clip);
	}) | rpl::start_with_next([=](QRect clip) {
		Painter p(_roundingOverlay);
		const auto skip = resizeArea();
		const auto outer = window()->rect();

		const auto rect = outer.marginsRemoved(skip);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		_roundRect.paint(p, rect, RectPart::AllCorners);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		const auto outside = std::array{
			QRect(0, 0, outer.width(), skip.top()),
			QRect(0, skip.top(), skip.left(), outer.height() - skip.top()),
			QRect(
				outer.width() - skip.right(),
				skip.top(),
				skip.right(),
				outer.height() - skip.top()),
			QRect(
				skip.left(),
				outer.height() - skip.bottom(),
				outer.width() - skip.left() - skip.right(),
				skip.bottom())
		};
		for (const auto &part : outside) {
			if (const auto fill = clip.intersected(part); !fill.isEmpty()) {
				p.fillRect(fill, Qt::transparent);
			}
		}

		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		Shadow::paint(p, rect, window()->width(), Shadow(), _sides, _corners);
	}, _roundingOverlay->lifetime());
}

not_null<RpWidget*> DefaultWindowHelper::body() {
	return _body;
}

QMargins DefaultWindowHelper::frameMargins() {
	return _title->isHidden()
		? BasicWindowHelper::nativeFrameMargins()
		: QMargins{ 0, _title->height(), 0, 0 };
}

bool DefaultWindowHelper::hasShadow() const {
	return WindowMarginsSupported() && TranslucentWindowsSupported();
}

QMargins DefaultWindowHelper::resizeArea() const {
	if (window()->isMaximized()
		|| window()->isFullScreen()
		|| _title->isHidden()
		|| (!hasShadow() && !_title->controlsGeometry().height())) {
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
	_title->setVisible(!enabled);
}

void DefaultWindowHelper::setMinimumSize(QSize size) {
	_minimumSize = size;
	window()->setMinimumSize(size.grownBy(bodyPadding()));
}

void DefaultWindowHelper::setFixedSize(QSize size) {
	_fixedSize = size;
	window()->setFixedSize(size.grownBy(bodyPadding()));
	_title->setResizeEnabled(false);
}

void DefaultWindowHelper::setGeometry(QRect rect) {
	window()->setGeometry(rect.marginsAdded(bodyPadding()));
}

int DefaultWindowHelper::manualRoundingRadius() const {
	return _roundingOverlay ? Radius() : 0;
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

void DefaultWindowHelper::updateWindowMargins() {
	if (hasShadow() && !_title->isHidden()) {
		SetWindowMargins(window(), resizeArea());
		_marginsSet = true;
	} else if (_marginsSet) {
		SetWindowMargins(window(), {});
		_marginsSet = false;
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
