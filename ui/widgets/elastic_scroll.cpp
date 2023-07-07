// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/elastic_scroll.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_common_adapters.h"
#include "styles/style_widgets.h"

#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kOverscrollReturnDuration = crl::time(250);
constexpr auto kOverscrollPower = 0.6;

} // namespace

// Flick scroll taken from
// http://qt-project.org/doc/qt-4.8
// /demos-embedded-anomaly-src-flickcharm-cpp.html

ElasticScrollBar::ElasticScrollBar(
	QWidget *parent,
	const style::ScrollArea &st,
	Qt::Orientation orientation)
: RpWidget(parent)
, _st(st)
, _hideTimer([=] { toggle(false); })
, _shown(!_st.hiding)
, _vertical(orientation == Qt::Vertical) {
}

void ElasticScrollBar::refreshGeometry() {
	update();
	const auto skip = _st.deltax;
	const auto fullSkip = _st.deltat + _st.deltab;
	const auto extSize = _vertical ? height() : width();
	const auto thickness = (_vertical ? width() : height()) - 2 * skip;
	const auto minSize = fullSkip + 2 * thickness;
	if (_state.fullSize <= 0
		|| _state.visibleFrom >= _state.visibleTill
		|| extSize < minSize) {
		_bar = _area = QRect();
		hide();
		return;
	}
	const auto available = extSize - fullSkip;
	const auto scale = [&](int value) {
		return (available * value) / _state.fullSize;
	};

	_area = _vertical
		? QRect(skip, _st.deltat, thickness, available)
		: QRect(_st.deltat, skip, available, thickness);
	const auto barMin = std::min(st::scrollBarMin, available / 2);
	const auto barWanted = scale(_state.visibleTill - _state.visibleFrom);
	if (barWanted >= available) {
		_bar = _area = QRect();
		hide();
		return;
	}
	const auto bar = std::max(barMin, barWanted);
	const auto barFrom = scale(_state.visibleFrom);
	const auto barTill = barFrom + bar;
	const auto cutFrom = std::clamp(barFrom, 0, available - thickness);
	const auto cutTill = std::clamp(barTill, thickness, available);
	const auto cutBar = cutTill - cutFrom;
	_bar = _vertical
		? QRect(_area.x(), _area.y() + cutFrom, _area.width(), cutBar)
		: QRect(_area.x() + cutFrom, _area.y(), cutBar, _area.height());
	if (isHidden()) {
		show();
	}
}

bool ElasticScrollBar::barHighlighted() const {
	return _overBar || _dragging;
}

void ElasticScrollBar::toggle(bool shown, anim::type animated) {
	const auto instant = (animated == anim::type::instant);
	const auto changed = (_shown != shown);
	_shown = shown;
	if (instant) {
		_shownAnimation.stop();
	}
	if (_shown && _st.hiding) {
		_hideTimer.callOnce(_st.hiding);
	}
	if (changed && !instant) {
		_shownAnimation.start(
			[=] { update(); },
			_shown ? 0. : 1.,
			_shown ? 1. : 0.,
			_st.duration);
	}
	update();
}

void ElasticScrollBar::toggleOver(bool over, anim::type animated) {
	const auto instant = (animated == anim::type::instant);
	const auto changed = (_over != over);
	_over = over;
	if (instant) {
		_overAnimation.stop();
	}
	if (!instant && changed) {
		_overAnimation.start(
			[=] { update(); },
			_over ? 0. : 1.,
			_over ? 1. : 0.,
			_st.duration);
	}
	update();
}

void ElasticScrollBar::toggleOverBar(bool over, anim::type animated) {
	const auto instant = (animated == anim::type::instant);
	const auto wasHighlight = barHighlighted();
	_overBar = over;
	if (instant) {
		_barHighlightAnimation.stop();
	} else {
		startBarHighlightAnimation(wasHighlight);
	}
	update();
}

void ElasticScrollBar::toggleDragging(bool dragging, anim::type animated) {
	const auto instant = (animated == anim::type::instant);
	const auto wasHighlight = barHighlighted();
	_dragging = dragging;
	if (instant) {
		_barHighlightAnimation.stop();
	} else {
		startBarHighlightAnimation(wasHighlight);
	}
	update();
}

void ElasticScrollBar::startBarHighlightAnimation(bool wasHighlighted) {
	if (barHighlighted() == wasHighlighted) {
		return;
	}
	const auto highlighted = !wasHighlighted;
	_barHighlightAnimation.start(
		[=] { update(); },
		highlighted ? 0. : 1.,
		highlighted ? 1. : 0.,
		_st.duration);
}

rpl::producer<int> ElasticScrollBar::visibleFromDragged() const {
	return _visibleFromDragged.events();
}

void ElasticScrollBar::updateState(ScrollState state) {
	if (_state != state) {
		_state = state;
		refreshGeometry();
		toggle(true);
	}
}

void ElasticScrollBar::paintEvent(QPaintEvent *e) {
	if (_bar.isEmpty()) {
		hide();
		return;
	}
	const auto barHighlight = _barHighlightAnimation.value(
		barHighlighted() ? 1. : 0.);
	const auto over = std::max(
		_overAnimation.value(_over ? 1. : 0.),
		barHighlight);
	const auto shown = std::max(
		_shownAnimation.value(_shown ? 1. : 0.),
		over);
	if (shown < 1. / 255) {
		return;
	}
	QPainter p(this);
	p.setPen(Qt::NoPen);
	auto bg = anim::color(_st.bg, _st.bgOver, over);
	bg.setAlpha(anim::interpolate(0, bg.alpha(), shown));
	auto bar = anim::color(_st.barBg, _st.barBgOver, barHighlight);
	bar.setAlpha(anim::interpolate(0, bar.alpha(), shown));
	const auto radius = (_st.round < 0)
		? (std::min(_area.width(), _area.height()) / 2.)
		: _st.round;
	if (radius) {
		PainterHighQualityEnabler hq(p);
		p.setBrush(bg);
		p.drawRoundedRect(_area, radius, radius);
		p.setBrush(bar);
		p.drawRoundedRect(_bar, radius, radius);
	} else {
		p.fillRect(_area, bg);
		p.fillRect(_bar, bar);
	}
}

void ElasticScrollBar::enterEventHook(QEnterEvent *e) {
	_hideTimer.cancel();
	setMouseTracking(true);
	toggleOver(true);
}

void ElasticScrollBar::leaveEventHook(QEvent *e) {
	if (!_dragging) {
		setMouseTracking(false);
	}
	toggleOver(false);
	toggleOverBar(false);
	if (_st.hiding && _shown) {
		_hideTimer.callOnce(_st.hiding);
	}
}

void ElasticScrollBar::mouseMoveEvent(QMouseEvent *e) {
	toggleOverBar(_bar.contains(e->pos()));
	if (_dragging && !_bar.isEmpty()) {
		const auto position = e->globalPos();
		const auto delta = position - _dragPosition;
		_dragPosition = position;
		if (auto change = _vertical ? delta.y() : delta.x()) {
			if (_dragOverscrollAccumulated * change < 0) {
				const auto overscroll = (change < 0)
					? std::max(_dragOverscrollAccumulated + change, 0)
					: std::min(_dragOverscrollAccumulated + change, 0);
				const auto delta = overscroll - _dragOverscrollAccumulated;
				_dragOverscrollAccumulated = overscroll;
				change -= delta;
			}
			if (change) {
				const auto now = std::clamp(
					_state.visibleFrom + change,
					std::min(_state.visibleFrom, 0),
					std::max(
						_state.visibleFrom,
						(_state.visibleFrom
							+ (_state.fullSize - _state.visibleTill))));
				const auto delta = now - _state.visibleFrom;
				if (change != delta) {
					_dragOverscrollAccumulated
						= ((_dragOverscrollAccumulated * change < 0)
							? change
							: (_dragOverscrollAccumulated + change));
				}
				_visibleFromDragged.fire_copy(now);
			}
		}
	}
}

void ElasticScrollBar::mousePressEvent(QMouseEvent *e) {
	if (_bar.isEmpty()) {
		return;
	}
	toggleDragging(true);
	_dragPosition = e->globalPos();
	_dragOverscrollAccumulated = 0;
	if (!_overBar) {
		const auto start = _vertical ? _area.y() : _area.x();
		const auto full = _vertical ? _area.height() : _area.width();
		const auto bar = _vertical ? _bar.height() : _bar.width();
		const auto half = bar / 2;
		const auto middle = std::clamp(
			_vertical ? e->pos().y() : e->pos().x(),
			start + half,
			start + full + half - bar);
		const auto range = _state.visibleFrom
			+ (_state.fullSize - _state.visibleTill);
		const auto from = range * (middle - half - start) / (full - bar);
		_visibleFromDragged.fire_copy(from);
	}
}

void ElasticScrollBar::mouseReleaseEvent(QMouseEvent *e) {
	toggleDragging(false);
	if (!_over) {
		setMouseTracking(false);
	}
}

void ElasticScrollBar::resizeEvent(QResizeEvent *e) {
	refreshGeometry();
}

ElasticScroll::ElasticScroll(
	QWidget *parent,
	const style::ScrollArea &st,
	Qt::Orientation orientation)
: RpWidget(parent)
, _st(st)
, _bar(std::make_unique<ElasticScrollBar>(this, _st, orientation))
, _touchTimer([=] { _touchRightButton = true; })
, _touchScrollTimer([=] { touchScrollTimer(); })
, _vertical(orientation == Qt::Vertical) {
	setAttribute(Qt::WA_AcceptTouchEvents);

	_bar->visibleFromDragged(
	) | rpl::start_with_next([=](int from) {
		tryScrollTo(from, false);
	}, _bar->lifetime());
}

void ElasticScroll::setHandleTouch(bool handle) {
	if (_touchDisabled != handle) {
		return;
	}
	_touchDisabled = !handle;
	constexpr auto attribute = Qt::WA_AcceptTouchEvents;
	setAttribute(attribute, handle);
	if (_widget) {
		if (handle) {
			_widgetAcceptsTouch = _widget->testAttribute(attribute);
			if (!_widgetAcceptsTouch) {
				_widget->setAttribute(attribute);
			}
		} else {
			if (!_widgetAcceptsTouch) {
				_widget->setAttribute(attribute, false);
			}
		}
	}
}

bool ElasticScroll::viewportEvent(QEvent *e) {
	return (e->type() == QEvent::Wheel)
		&& handleWheelEvent(static_cast<QWheelEvent*>(e));
}

void ElasticScroll::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

int ElasticScroll::overscrollAmount() const {
	return (_state.visibleFrom < 0)
		? _state.visibleFrom
		: (_state.visibleTill > _state.fullSize)
		? (_state.visibleTill - _state.fullSize)
		: 0;
}

void ElasticScroll::overscrollReturn() {
	_ignoreMomentum = _overscrollReturning = true;
	if (overscrollFinish()) {
		_overscrollReturnAnimation.stop();
		return;
	} else if (_overscrollReturnAnimation.animating()) {
		return;
	}
	_overscrollReturnAnimation.start(
		[=] { applyAccumulatedScroll(); },
		0.,
		1.,
		kOverscrollReturnDuration,
		anim::sineInOut);
}

void ElasticScroll::overscrollReturnCancel() {
	if (_overscrollReturning) {
		const auto returnProgress = _overscrollReturnAnimation.value(1.);
		_overscrollAccumulated *= (1. - returnProgress);
		_overscrollReturning = false;
		_overscrollReturnAnimation.stop();
		applyAccumulatedScroll();
	}
}

bool ElasticScroll::overscrollFinish() {
	if (overscrollAmount()) {
		return false;
	}
	_overscrollReturning = false;
	_overscrollAccumulated = 0;
	return true;
}

void ElasticScroll::innerResized() {
	_innerResizes.fire({});
}

int ElasticScroll::scrollWidth() const {
	return (_vertical || !_widget)
		? width()
		: std::max(_widget->width(), width());
}

int ElasticScroll::scrollHeight() const {
	return (!_vertical || !_widget)
		? height()
		: std::max(_widget->height(), height());
}

int ElasticScroll::scrollLeftMax() const {
	return scrollWidth() - width();
}

int ElasticScroll::scrollTopMax() const {
	return scrollHeight() - height();
}

int ElasticScroll::scrollLeft() const {
	return _vertical ? 0 : _state.visibleFrom;
}

int ElasticScroll::scrollTop() const {
	return _vertical ? _state.visibleFrom : 0;
}

void ElasticScroll::touchScrollTimer() {
	auto nowTime = crl::now();
	if (_touchScrollState == TouchScrollState::Acceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = TouchScrollState::Manual;
		touchResetSpeed();
	} else if (_touchScrollState == TouchScrollState::Auto || _touchScrollState == TouchScrollState::Acceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = touchScroll(delta);

		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = TouchScrollState::Manual;
			_touchScroll = false;
			_touchScrollTimer.cancel();
		} else {
			_touchTime = nowTime;
		}
		touchDeaccelerate(elapsed);
	}
}

void ElasticScroll::touchUpdateSpeed() {
	const auto nowTime = crl::now();
	if (_touchPreviousPositionValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPosition - _touchPreviousPosition);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > kFingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > kFingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == TouchScrollState::Auto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(std::clamp((oldSpeedY + (newSpeedY / 4)), -kMaxScrollAccelerated, +kMaxScrollAccelerated));
					_touchSpeed.setX(std::clamp((oldSpeedX + (newSpeedX / 4)), -kMaxScrollAccelerated, +kMaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				// we average the speed to avoid strange effects with the last delta
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(std::clamp((_touchSpeed.x() / 4) + (newSpeedX * 3 / 4), -kMaxScrollFlick, +kMaxScrollFlick));
					_touchSpeed.setY(std::clamp((_touchSpeed.y() / 4) + (newSpeedY * 3 / 4), -kMaxScrollFlick, +kMaxScrollFlick));
				} else {
					_touchSpeed = QPoint(newSpeedX, newSpeedY);
				}
			}
		}
	} else {
		_touchPreviousPositionValid = true;
	}
	_touchSpeedTime = nowTime;
	_touchPreviousPosition = _touchPosition;
}

void ElasticScroll::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPreviousPositionValid = false;
}

bool ElasticScroll::eventHook(QEvent *e) {
	return filterOutTouchEvent(e) || RpWidget::eventHook(e);
}

void ElasticScroll::wheelEvent(QWheelEvent *e) {
	if (handleWheelEvent(e)) {
		e->accept();
	} else {
		e->ignore();
	}
}

void ElasticScroll::paintEvent(QPaintEvent *e) {
	if (!_overscrollBg) {
		return;
	}
	const auto fillFrom = std::max(-_state.visibleFrom, 0);
	const auto fillTill = std::max(_state.visibleTill - _state.fullSize, 0);
	if (!fillFrom && !fillTill) {
		return;
	}
	auto p = QPainter(this);
	if (fillFrom) {
		p.fillRect(
			0,
			0,
			_vertical ? width() : fillFrom,
			_vertical ? fillFrom : height(),
			*_overscrollBg);
	}
	if (fillTill) {
		p.fillRect(
			_vertical ? 0 : (width() - fillTill),
			_vertical ? (height() - fillTill) : 0,
			_vertical ? width() : fillTill,
			_vertical ? fillTill : height(),
			*_overscrollBg);
	}
}

bool ElasticScroll::handleWheelEvent(not_null<QWheelEvent*> e) {
	if (_customWheelProcess
		&& _customWheelProcess(static_cast<QWheelEvent*>(e.get()))) {
		return true;
	}
	const auto phase = e->phase();
	const auto momentum = (phase == Qt::ScrollMomentum)
		|| (phase == Qt::ScrollEnd);
	const auto now = crl::now();
	const auto guard = gsl::finally([&] {
		_lastScroll = now;
	});
	if (_ignoreMomentum) {
		if (momentum) {
			if (!_overscrollReturnAnimation.animating()) {
				return true;
			}
		} else {
			_ignoreMomentum = false;
		}
	}
	const auto pixels = ScrollDelta(e);
	const auto amount = overscrollAmount();
	auto delta = _vertical ? -pixels.y() : pixels.x();
	if (phase == Qt::NoScrollPhase) {
		if (!amount) {
			tryScrollTo(_state.visibleFrom + delta);
		} else if (!_overscrollReturnAnimation.animating()) {
			overscrollReturn();
		}
		return true;
	}
	if (!momentum) {
		overscrollReturnCancel();
	} else if (amount && !_overscrollReturnAnimation.animating()) {
		overscrollReturn();
	}
	if (!amount) {
		const auto normalTo = willScrollTo(_state.visibleFrom + delta);
		delta -= normalTo - _state.visibleFrom;
		applyScrollTo(normalTo);
	}
	if (!delta) {
		return true;
	}
	const auto accumulated = _overscrollAccumulated + delta;
	if (_overscrollAccumulated * accumulated < 0) {
		_overscrollAccumulated = 0;
	} else {
		_overscrollAccumulated = accumulated;
	}
	applyAccumulatedScroll();
	return true;
}

void ElasticScroll::applyAccumulatedScroll() {
	if (_overscrollReturning) {
		if (!_overscrollReturnAnimation.animating()) {
			_overscrollReturning = false;
			_overscrollAccumulated = 0;
		} else if (overscrollFinish()) {
			_overscrollReturnAnimation.stop();
		}
	}
	const auto returnProgress = _overscrollReturnAnimation.value(
		_overscrollReturning ? 1. : 0.);
	const auto accumulated = (1. - returnProgress) * _overscrollAccumulated;
	const auto byAccumulated = (accumulated > 0 ? 1. : -1.)
		* int(base::SafeRound(pow(std::abs(accumulated), kOverscrollPower)));

	applyScrollTo(_state.visibleFrom - overscrollAmount() + byAccumulated);
}

bool ElasticScroll::eventFilter(QObject *obj, QEvent *e) {
	const auto result = RpWidget::eventFilter(obj, e);
	if (obj == _widget.data()) {
		if (filterOutTouchEvent(e)) {
			return true;
		} else if (e->type()  == QEvent::Resize) {
			const auto weak = Ui::MakeWeak(this);
			updateState();
			if (weak) {
				_innerResizes.fire({});
			}
		} else if (e->type()  == QEvent::Move) {
			updateState();
		}
		return result;
	}
	return false;
}

bool ElasticScroll::filterOutTouchEvent(QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		const auto ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			if (_customTouchProcess && _customTouchProcess(ev)) {
				return true;
			} else if (!_touchDisabled) {
				handleTouchEvent(ev);
				return true;
			}
		}
	}
	return false;
}

void ElasticScroll::handleTouchEvent(QTouchEvent *e) {
	if (!e->touchPoints().isEmpty()) {
		_touchPreviousPosition = _touchPosition;
		_touchPosition = e->touchPoints().cbegin()->screenPos().toPoint();
	}

	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchPress = true;
		if (_touchScrollState == TouchScrollState::Auto) {
			_touchScrollState = TouchScrollState::Acceleration;
			_touchWaitingAcceleration = true;
			_touchAccelerationTime = crl::now();
			touchUpdateSpeed();
			_touchStart = _touchPosition;
		} else {
			_touchScroll = false;
			_touchTimer.callOnce(QApplication::startDragTime());
		}
		_touchStart = _touchPreviousPosition = _touchPosition;
		_touchRightButton = false;
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchPress) return;
		if (!_touchScroll && (_touchPosition - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchTimer.cancel();
			_touchScroll = true;
			touchUpdateSpeed();
		}
		if (_touchScroll) {
			if (_touchScrollState == TouchScrollState::Manual) {
				touchScrollUpdated(_touchPosition);
			} else if (_touchScrollState == TouchScrollState::Acceleration) {
				touchUpdateSpeed();
				_touchAccelerationTime = crl::now();
				if (_touchSpeed.isNull()) {
					_touchScrollState = TouchScrollState::Manual;
				}
			}
		}
	} break;

	case QEvent::TouchEnd: {
		if (!_touchPress) return;
		_touchPress = false;
		auto weak = MakeWeak(this);
		if (_touchScroll) {
			if (_touchScrollState == TouchScrollState::Manual) {
				_touchScrollState = TouchScrollState::Auto;
				_touchPreviousPositionValid = false;
				_touchScrollTimer.callEach(15);
				_touchTime = crl::now();
			} else if (_touchScrollState == TouchScrollState::Auto) {
				_touchScrollState = TouchScrollState::Manual;
				_touchScroll = false;
				touchResetSpeed();
			} else if (_touchScrollState == TouchScrollState::Acceleration) {
				_touchScrollState = TouchScrollState::Auto;
				_touchWaitingAcceleration = false;
				_touchPreviousPositionValid = false;
			}
		} else if (window()) { // one short tap -- like left mouse click, one long tap -- like right mouse click
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);

			if (weak) SendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton, _touchStart);
			if (weak) SendSynteticMouseEvent(this, QEvent::MouseButtonPress, btn, _touchStart);
			if (weak) SendSynteticMouseEvent(this, QEvent::MouseButtonRelease, btn, _touchStart);

			if (weak && _touchRightButton) {
				auto windowHandle = window()->windowHandle();
				auto localPoint = windowHandle->mapFromGlobal(_touchStart);
				QContextMenuEvent ev(QContextMenuEvent::Mouse, localPoint, _touchStart, QGuiApplication::keyboardModifiers());
				ev.setTimestamp(crl::now());
				QGuiApplication::sendEvent(windowHandle, &ev);
			}
		}
		if (weak) {
			_touchTimer.cancel();
			_touchRightButton = false;
		}
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchScroll = false;
		_touchScrollState = TouchScrollState::Manual;
		_touchTimer.cancel();
	} break;
	}
}

void ElasticScroll::touchScrollUpdated(const QPoint &screenPos) {
	_touchPosition = screenPos;
	touchScroll(_touchPosition - _touchPreviousPosition);
	touchUpdateSpeed();
}

void ElasticScroll::disableScroll(bool dis) {
	_disabled = dis;
	if (_disabled && _st.hiding) {
		_bar->toggle(false);
	}
}

void ElasticScroll::updateState() {
	_dirtyState = false;
	if (!_widget) {
		setState({});
		return;
	}
	auto from = _vertical ? -_widget->y() : -_widget->x();
	auto till = from + (_vertical ? height() : width());
	const auto wasFullSize = _state.fullSize;
	const auto nowFullSize = _vertical ? scrollHeight() : scrollWidth();
	if (wasFullSize > nowFullSize) {
		const auto wasOverscroll = std::max(_state.visibleTill - wasFullSize, 0);
		const auto nowOverscroll = std::max(till - nowFullSize, 0);
		const auto delta = std::max(
			std::min(nowOverscroll - wasOverscroll, from),
			0);
		from -= delta;
		till -= delta;
	}
	setState({
		.visibleFrom = from,
		.visibleTill = till,
		.fullSize = nowFullSize,
	});
}

void ElasticScroll::setState(ScrollState state) {
	if (_state == state) {
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	const auto old = _state.visibleFrom;
	_state = state;
	_bar->updateState(state);
	if (weak && _state.visibleFrom != old) {
		if (_vertical) {
			_scrollTopUpdated.fire_copy(_state.visibleFrom);
		}
		if (weak) {
			_scrolls.fire({});
		}
	}
}

void ElasticScroll::applyScrollTo(int position, bool synthMouseMove) {
	if (_disabled) {
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	_dirtyState = true;
	_widget->move(
		_vertical ? _widget->x() : -position,
		_vertical ? -position : _widget->y());
	if (weak) {
		if (_dirtyState) {
			updateState();
		}
		if (weak && synthMouseMove) {
			SendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
		}
	}
}

int ElasticScroll::willScrollTo(int position) const {
	return std::clamp(
		position,
		std::min(_state.visibleFrom, 0),
		std::max(
			_state.visibleFrom,
			(_state.visibleFrom
				+ (_state.fullSize - _state.visibleTill))));
}

void ElasticScroll::tryScrollTo(int position, bool synthMouseMove) {
	applyScrollTo(willScrollTo(position), synthMouseMove);
}

bool ElasticScroll::touchScroll(const QPoint &delta) {
	const auto scTop = scrollTop();
	const auto scMax = scrollTopMax();
	const auto scNew = std::clamp(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) {
		return false;
	}
	scrollToY(scNew);
	return true;
}

void ElasticScroll::resizeEvent(QResizeEvent *e) {
	const auto rtl = (layoutDirection() == Qt::RightToLeft);
	_bar->setGeometry(_vertical
		? QRect(
			(rtl ? 0 : (width() - _st.width)),
			0,
			_st.width,
			height())
		: QRect(0, height() - _st.width, width(), _st.width));
	_geometryChanged.fire({});
}

void ElasticScroll::moveEvent(QMoveEvent *e) {
	_geometryChanged.fire({});
}

void ElasticScroll::keyPressEvent(QKeyEvent *e) {
	if ((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)
		&& e->modifiers().testFlag(Qt::AltModifier)) {
		e->ignore();
	} else if (_widget && (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back)) {
		((QObject*)_widget.data())->event(e);
	}
}

void ElasticScroll::enterEventHook(QEnterEvent *e) {
	if (!_disabled) {
		_bar->toggle(true);
	}
}

void ElasticScroll::leaveEventHook(QEvent *e) {
	_bar->toggle(false);
}

void ElasticScroll::scrollTo(ScrollToRequest request) {
	scrollToY(request.ymin, request.ymax);
}

void ElasticScroll::scrollToWidget(not_null<QWidget*> widget) {
	if (const auto local = _widget.data()) {
		const auto position = Ui::MapFrom(
			local,
			widget.get(),
			QPoint(0, 0));
		const auto from = _vertical ? position.y() : position.x();
		const auto till = _vertical
			? (position.y() + widget->height())
			: (position.x() + widget->width());
		scrollToY(from, till);
	}
}

void ElasticScroll::scrollToY(int toTop, int toBottom) {
	if (_vertical) {
		scrollTo(toTop, toBottom);
	}
}

void ElasticScroll::scrollTo(int toFrom, int toTill) {
	if (const auto inner = _widget.data()) {
		SendPendingMoveResizeEvents(inner);
	}
	SendPendingMoveResizeEvents(this);

	int toMin = std::min(_state.visibleFrom, 0);
	int toMax = std::max(
		_state.visibleFrom,
		_state.visibleFrom + _state.fullSize - _state.visibleTill);
	if (toFrom < toMin) {
		toFrom = toMin;
	} else if (toFrom > toMax) {
		toFrom = toMax;
	}
	bool exact = (toTill < 0);

	int curFrom = _state.visibleFrom, curRange = _state.visibleTill - _state.visibleFrom, curTill = curFrom + curRange, scTo = toFrom;
	if (!exact && toFrom >= curFrom) {
		if (toTill < toFrom) toTill = toFrom;
		if (toTill <= curTill) return;

		scTo = toTill - curRange;
		if (scTo > toFrom) scTo = toFrom;
		if (scTo == curFrom) return;
	} else {
		scTo = toFrom;
	}
	applyScrollTo(scTo);
}

void ElasticScroll::doSetOwnedWidget(object_ptr<QWidget> w) {
	constexpr auto attribute = Qt::WA_AcceptTouchEvents;
	if (_widget) {
		_widget->removeEventFilter(this);
		if (!_touchDisabled && !_widgetAcceptsTouch) {
			_widget->setAttribute(attribute, false);
		}
	}
	_widget = std::move(w);
	if (_widget) {
		if (_widget->parentWidget() != this) {
			_widget->setParent(this);
			_widget->show();
		}
		_bar->raise();
		_widget->installEventFilter(this);
		if (!_touchDisabled) {
			_widgetAcceptsTouch = _widget->testAttribute(attribute);
			if (!_widgetAcceptsTouch) {
				_widget->setAttribute(attribute);
			}
		}
		updateState();
	}
}

object_ptr<QWidget> ElasticScroll::doTakeWidget() {
	return std::move(_widget);
}

void ElasticScroll::updateBars() {
	_bar->update();
}

rpl::producer<> ElasticScroll::scrolls() const {
	return _scrolls.events();
}

rpl::producer<> ElasticScroll::innerResizes() const {
	return _innerResizes.events();
}

rpl::producer<> ElasticScroll::geometryChanged() const {
	return _geometryChanged.events();
}

QPoint ScrollDelta(not_null<QWheelEvent*> e) {
	const auto convert = [](QPoint point) {
		return QPoint(
			style::ConvertScale(point.x()),
			style::ConvertScale(point.y()));
	};
	if (Platform::IsMac()
		|| (Platform::IsWindows() && e->phase() != Qt::NoScrollPhase)) {
		return convert(e->pixelDelta());
	}
	return convert(e->angleDelta() * style::DevicePixelRatio());
}

} // namespace Ui
