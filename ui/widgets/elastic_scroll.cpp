// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/elastic_scroll.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/qt_weak_factory.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_common_adapters.h"
#include "styles/style_widgets.h"

#include <QtGui/QWindow>
#include <QtCore/QtMath>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kOverscrollReturnDuration = crl::time(250);
//constexpr auto kOverscrollPower = 0.6;
constexpr auto kOverscrollFromThreshold = -(1 << 30);
constexpr auto kOverscrollTillThreshold = (1 << 30);
constexpr auto kTouchOverscrollMultiplier = 2;
constexpr auto kMagicScrollMultiplier = 2.5;
constexpr auto kDefaultWheelScrollLines = 3;

constexpr auto kLogA = 16.;
constexpr auto kLogB = 10.;

[[nodiscard]] float64 RawFrom(float64 value) {
	const auto scale = style::Scale() / 100.;
	value /= scale;

	const auto result = kLogA * log(1. + value / kLogB);
	//const auto result = pow(value, kOverscrollPower);

	return result * scale;
}

[[nodiscard]] float64 RawTo(float64 value) {
	const auto scale = style::Scale() / 100.;
	value /= scale;

	const auto result = (exp(value / kLogA) - 1.) * kLogB;
	//const auto result = pow(value, 1. / kOverscrollPower);

	return result * scale;
}

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
	setAttribute(Qt::WA_NoMousePropagation);
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
	_area = _vertical
		? QRect(skip, _st.deltat, thickness, available)
		: QRect(_st.deltat, skip, available, thickness);
	const auto barMin = std::min(st::scrollBarMin, available / 2);
	const auto visibleHeight = _state.visibleTill - _state.visibleFrom;
	const auto scrollableHeight = _state.fullSize - visibleHeight;
	const auto barWanted = (available * visibleHeight) / _state.fullSize;
	if (barWanted >= available) {
		_bar = _area = QRect();
		hide();
		return;
	}
	const auto bar = std::max(barMin, barWanted);
	const auto outsideBar = available - bar;

	const auto scale = [&](int value) {
		return (outsideBar * value) / scrollableHeight;
	};
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

int ElasticScrollBar::scaleToBar(int change) const {
	const auto scrollable = _state.fullSize
		- (_state.visibleTill - _state.visibleFrom);
	const auto outsideBar = (_vertical ? _area.height() : _area.width())
		- (_vertical ? _bar.height() : _bar.width());
	return (outsideBar <= 0 || scrollable <= outsideBar)
		? change
		: (change * scrollable / outsideBar);
}

void ElasticScrollBar::mouseMoveEvent(QMouseEvent *e) {
	toggleOverBar(_bar.contains(e->pos()));
	if (_dragging && !_bar.isEmpty()) {
		const auto position = e->globalPos();
		const auto delta = position - _dragPosition;
		_dragPosition = position;
		if (auto change = scaleToBar(_vertical ? delta.y() : delta.x())) {
			if (base::OppositeSigns(_dragOverscrollAccumulated, change)) {
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
						= (base::OppositeSigns(
							_dragOverscrollAccumulated,
							change)
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

bool ElasticScrollBar::eventHook(QEvent *e) {
	setAttribute(Qt::WA_NoMousePropagation, e->type() != QEvent::Wheel);
	return RpWidget::eventHook(e);
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
, _vertical(orientation == Qt::Vertical)
, _position(Position{ 0, 0 })
, _movement(Movement::None) {
	setAttribute(Qt::WA_AcceptTouchEvents);

	_bar->visibleFromDragged(
	) | rpl::start_with_next([=](int from) {
		tryScrollTo(from, false);
	}, _bar->lifetime());
}

ElasticScroll::~ElasticScroll() {
	// Destroy the _bar cleanly (keeping _bar == nullptr) to avoid a crash:
	//
	// _bar destructor may send LeaveEvent to ElasticScroll,
	// which will try to toggle(false) the _bar in leaveEventHook.
	base::take(_bar);
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
	const auto type = e->type();
	if (type == QEvent::Wheel) {
		return handleWheelEvent(static_cast<QWheelEvent*>(e));
	} else if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		handleTouchEvent(static_cast<QTouchEvent*>(e));
		return true;
	}
	return false;
}

void ElasticScroll::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void ElasticScroll::overscrollReturn() {
	_overscrollReturning = true;
	_ignoreMomentumFromOverscroll = _overscroll;
	if (overscrollFinish()) {
		_overscrollReturnAnimation.stop();
		return;
	} else if (_overscrollReturnAnimation.animating()) {
		return;
	}
	_movement = Movement::Returning;
	_overscrollReturnAnimation.start(
		[=] { applyAccumulatedScroll(); },
		0.,
		1.,
		kOverscrollReturnDuration,
		anim::sineInOut);
}

auto ElasticScroll::computeAccumulatedParts() const ->AccumulatedParts {
	const auto baseAccumulated = currentOverscrollDefaultAccumulated();
	const auto returnProgress = _overscrollReturnAnimation.value(
		_overscrollReturning ? 1. : 0.);
	const auto relativeAccumulated = (1. - returnProgress)
		* (_overscrollAccumulated - baseAccumulated);
	return {
		.base = baseAccumulated,
		.relative = int(base::SafeRound(relativeAccumulated)),
	};
}

void ElasticScroll::overscrollReturnCancel() {
	_movement = Movement::Progress;
	if (_overscrollReturning) {
		const auto parts = computeAccumulatedParts();
		_overscrollAccumulated = parts.base + parts.relative;
		_overscrollReturnAnimation.stop();
		_overscrollReturning = false;
		applyAccumulatedScroll();
	}
}

int ElasticScroll::currentOverscrollDefault() const {
	return (_overscroll < 0)
		? _overscrollDefaultFrom
		: (_overscroll > 0)
		? _overscrollDefaultTill
		: 0;
}

int ElasticScroll::currentOverscrollDefaultAccumulated() const {
	return (_overscrollAccumulated < 0)
		? (_overscrollDefaultFrom ? kOverscrollFromThreshold : 0)
		: (_overscrollAccumulated > 0)
		? (_overscrollDefaultTill ? kOverscrollTillThreshold : 0)
		: 0;
}

void ElasticScroll::overscrollCheckReturnFinish() {
	if (!_overscrollReturning) {
		return;
	} else if (!_overscrollReturnAnimation.animating()) {
		_overscrollReturning = false;
		_overscrollAccumulated = currentOverscrollDefaultAccumulated();
		_movement = Movement::None;
	} else if (overscrollFinish()) {
		_overscrollReturnAnimation.stop();
	}
}

bool ElasticScroll::overscrollFinish() {
	if (_overscroll != currentOverscrollDefault()) {
		return false;
	}
	_overscrollReturning = false;
	_overscrollAccumulated = currentOverscrollDefaultAccumulated();
	_movement = Movement::None;
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
		sendWheelEvent(Qt::ScrollEnd);
		touchResetSpeed();
	} else if (_touchScrollState == TouchScrollState::Auto || _touchScrollState == TouchScrollState::Acceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		sendWheelEvent(
			_touchPress ? Qt::ScrollUpdate : Qt::ScrollMomentum,
			delta);

		if (_touchSpeed.isNull()) {
			_touchScrollState = TouchScrollState::Manual;
			sendWheelEvent(Qt::ScrollEnd);
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
	const auto content = _widget
		? (_vertical ? _widget->height() : _widget->width())
		: 0;
	const auto fillTill = content
		? std::max(_state.visibleTill - content, 0)
		: (_vertical ? height() : width());
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

bool ElasticScroll::handleWheelEvent(not_null<QWheelEvent*> e, bool touch) {
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
	const auto unmultiplied = ScrollDelta(e, touch);
	const auto multiply = e->modifiers()
		& (Qt::ControlModifier | Qt::ShiftModifier);
	const auto pixels = multiply
		? QPoint(
			(unmultiplied.x() * std::max(width(), 120) / 120.),
			(unmultiplied.y() * std::max(height(), 120) / 120.))
		: unmultiplied;
	auto delta = _vertical ? -pixels.y() : pixels.x();
	if (std::abs(_vertical ? pixels.x() : pixels.y()) >= std::abs(delta)) {
		delta = 0;
	}
	if (_ignoreMomentumFromOverscroll) {
		if (!momentum) {
			_ignoreMomentumFromOverscroll = 0;
		} else if (!_overscrollReturnAnimation.animating()
			&& !base::OppositeSigns(_ignoreMomentumFromOverscroll, delta)) {
			return true;
		}
	}
	if (phase == Qt::NoScrollPhase) {
		if (_overscroll == currentOverscrollDefault()) {
			tryScrollTo(_state.visibleFrom + delta);
			_movement = Movement::None;
		} else if (!_overscrollReturnAnimation.animating()) {
			overscrollReturn();
		}
		return true;
	}
	if (!momentum) {
		overscrollReturnCancel();
	} else if (_overscroll != currentOverscrollDefault()
		&& !_overscrollReturnAnimation.animating()) {
		overscrollReturn();
	} else if (!_overscrollReturnAnimation.animating()) {
		_movement = (phase == Qt::ScrollEnd)
			? Movement::None
			: Movement::Momentum;
	}
	if (!_overscroll) {
		const auto normalTo = willScrollTo(_state.visibleFrom + delta);
		delta -= normalTo - _state.visibleFrom;
		applyScrollTo(normalTo);
	}
	if (!delta) {
		return true;
	}
	if (touch) {
		delta *= kTouchOverscrollMultiplier;
	}
	const auto accumulated = _overscrollAccumulated + delta;
	const auto type = (accumulated < 0)
		? _overscrollTypeFrom
		: (accumulated > 0)
		? _overscrollTypeTill
		: OverscrollType::None;
	if (type == OverscrollType::None
		|| base::OppositeSigns(_overscrollAccumulated, accumulated)) {
		_overscrollAccumulated = 0;
	} else {
		_overscrollAccumulated = accumulated;
	}
	applyAccumulatedScroll();
	return true;
}

void ElasticScroll::applyAccumulatedScroll() {
	overscrollCheckReturnFinish();
	const auto parts = computeAccumulatedParts();
	const auto baseOverscroll = (_overscrollAccumulated < 0)
		? _overscrollDefaultFrom
		: (_overscrollAccumulated > 0)
		? _overscrollDefaultTill
		: 0;
	applyOverscroll(baseOverscroll
		+ OverscrollFromAccumulated(parts.relative));
}

bool ElasticScroll::eventFilter(QObject *obj, QEvent *e) {
	const auto result = RpWidget::eventFilter(obj, e);
	if (obj == _widget.data()) {
		if (filterOutTouchEvent(e)) {
			return true;
		} else if (e->type() == QEvent::Resize) {
			const auto weak = Ui::MakeWeak(this);
			updateState();
			if (weak) {
				_innerResizes.fire({});
			}
		} else if (e->type() == QEvent::Move) {
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
		if ((ev->type() == QEvent::TouchCancel && !ev->device())
			|| (ev->device()->type() == base::TouchDevice::TouchScreen)) {
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
		if (_touchPress || e->touchPoints().isEmpty()) {
			return;
		}
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
		sendWheelEvent(Qt::ScrollBegin);
	} break;

	case QEvent::TouchUpdate: {
		if (!_touchPress) {
			return;
		}
		if (!_touchScroll
			&& ((_touchPosition - _touchStart).manhattanLength()
				>= QApplication::startDragDistance())) {
			_touchTimer.cancel();
			_touchScroll = true;
			touchUpdateSpeed();
		}
		if (_touchScroll) {
			if (_touchScrollState == TouchScrollState::Manual) {
				touchScrollUpdated();
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
		if (!_touchPress) {
			return;
		}
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

void ElasticScroll::touchScrollUpdated() {
	//touchScroll(_touchPosition - _touchPreviousPosition);
	const auto phase = !_touchPress
		? Qt::ScrollMomentum
		: Qt::ScrollUpdate;
	sendWheelEvent(phase, _touchPosition - _touchPreviousPosition);
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
		const auto wasOverscroll = std::max(
			_state.visibleTill - wasFullSize,
			0);
		const auto nowOverscroll = std::max(till - nowFullSize, 0);
		const auto delta = std::max(
			std::min(nowOverscroll - wasOverscroll, from),
			0);
		if (delta) {
			applyScrollTo(from - delta);
			return;
		}
	}
	setState({
		.visibleFrom = from,
		.visibleTill = till,
		.fullSize = nowFullSize,
	});
}

void ElasticScroll::setState(ScrollState state) {
	if (_overscroll < 0
		&& (state.visibleFrom > 0
			|| (!state.visibleFrom
				&& _overscrollTypeFrom == OverscrollType::Real))) {
		_overscroll = _overscrollDefaultFrom = 0;
		overscrollFinish();
		_overscrollReturnAnimation.stop();
	} else if (_overscroll > 0
		&& (state.visibleTill < state.fullSize
			|| (state.visibleTill == state.fullSize
				&& _overscrollTypeTill == OverscrollType::Real))) {
		_overscroll = _overscrollDefaultTill = 0;
		overscrollFinish();
		_overscrollReturnAnimation.stop();
	}
	if (_state == state) {
		_position = Position{ _state.visibleFrom, _overscroll };
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	const auto old = _state.visibleFrom;
	_state = state;
	_bar->updateState(state);
	if (weak) {
		_position = Position{ _state.visibleFrom, _overscroll };
	}
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
	const auto was = _widget->geometry();
	_widget->move(
		_vertical ? _widget->x() : -position,
		_vertical ? -position : _widget->y());
	if (weak) {
		const auto now = _widget->geometry();
		const auto wasFrom = _vertical ? was.y() : was.x();
		const auto wasTill = wasFrom
			+ (_vertical ? was.height() : was.width());
		const auto nowFrom = _vertical ? now.y() : now.x();
		const auto nowTill = nowFrom
			+ (_vertical ? now.height() : now.width());
		const auto mySize = _vertical ? height() : width();
		if ((wasFrom > 0 && wasFrom < mySize)
			|| (wasTill > 0 && wasTill < mySize)
			|| (nowFrom > 0 && nowFrom < mySize)
			|| (nowTill > 0 && nowTill < mySize)) {
			update();
		}
		if (_dirtyState) {
			updateState();
		}
		if (weak && synthMouseMove) {
			SendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
		}
	}
}

void ElasticScroll::applyOverscroll(int overscroll) {
	if (_overscroll == overscroll) {
		return;
	}
	_overscroll = overscroll;
	const auto max = _state.fullSize
		- (_state.visibleTill - _state.visibleFrom);
	if (_overscroll > 0) {
		const auto added = (_overscrollTypeTill == OverscrollType::Real)
			? _overscroll
			: 0;
		applyScrollTo(max + added);
	} else if (_overscroll < 0) {
		applyScrollTo((_overscrollTypeFrom == OverscrollType::Real)
			? _overscroll
			: 0);
	} else {
		applyScrollTo(std::clamp(_state.visibleFrom, 0, max));
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

void ElasticScroll::sendWheelEvent(Qt::ScrollPhase phase, QPoint delta) {
	auto e = QWheelEvent(
		mapFromGlobal(_touchPosition),
		_touchPosition,
		delta,
		delta,
		Qt::NoButton,
		Qt::KeyboardModifiers(), // Ignore Ctrl/Shift fast scroll on touch.
		phase,
		false,
		Qt::MouseEventSynthesizedByApplication);
	handleWheelEvent(&e, true);
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
	updateState();
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
	if (_bar && !_disabled) {
		_bar->toggle(true);
	}
}

void ElasticScroll::leaveEventHook(QEvent *e) {
	if (_bar) {
		_bar->toggle(false);
	}
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

void ElasticScroll::setOverscrollTypes(
		OverscrollType from,
		OverscrollType till) {
	const auto fromChanged = (_overscroll < 0)
		&& (_overscrollTypeFrom != from);
	const auto tillChanged = (_overscroll > 0)
		&& (_overscrollTypeTill != till);
	_overscrollTypeFrom = from;
	_overscrollTypeTill = till;
	if (fromChanged) {
		switch (_overscrollTypeFrom) {
		case OverscrollType::None:
			_overscroll = _overscrollAccumulated = 0;
			applyScrollTo(0);
			break;
		case OverscrollType::Virtual:
			applyScrollTo(0);
			break;
		case OverscrollType::Real:
			applyScrollTo(_overscroll);
			break;
		}
	} else if (tillChanged) {
		const auto max = _state.fullSize
			- (_state.visibleTill - _state.visibleFrom);
		switch (_overscrollTypeTill) {
		case OverscrollType::None:
			_overscroll = _overscrollAccumulated = 0;
			applyScrollTo(max);
			break;
		case OverscrollType::Virtual:
			applyScrollTo(max);
			break;
		case OverscrollType::Real:
			applyScrollTo(max + _overscroll);
			break;
		}
	}
}

void ElasticScroll::setOverscrollDefaults(int from, int till, bool shift) {
	Expects(from <= 0 && till >= 0);

	if (_state.visibleFrom > 0
		|| (!_state.visibleFrom
			&& _overscrollTypeFrom != OverscrollType::Virtual)) {
		from = 0;
	}
	if (_state.visibleTill < _state.fullSize
		|| (_state.visibleTill == _state.fullSize
			&& _overscrollTypeTill != OverscrollType::Virtual)) {
		till = 0;
	}
	const auto fromChanged = (_overscrollDefaultFrom != from);
	const auto tillChanged = (_overscrollDefaultTill != till);
	const auto changed = (fromChanged && _overscroll < 0)
		|| (tillChanged && _overscroll > 0);
	const auto movement = _movement.current();
	if (_overscrollReturnAnimation.animating()) {
		overscrollReturnCancel();
	}
	_overscrollDefaultFrom = from;
	_overscrollDefaultTill = till;
	if (changed) {
		const auto delta = (_overscroll < 0)
			? (_overscroll - (shift ? 0 : _overscrollDefaultFrom))
			: (_overscroll - (shift ? 0 : _overscrollDefaultTill));
		_overscrollAccumulated = currentOverscrollDefaultAccumulated()
			+ OverscrollToAccumulated(delta);
	}
	if (movement == Movement::Momentum || movement == Movement::Returning) {
		if (_overscroll != currentOverscrollDefault()) {
			overscrollReturn();
		}
	}
}

void ElasticScroll::setOverscrollBg(QColor bg) {
	_overscrollBg = bg;
	update();
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

ElasticScrollPosition ElasticScroll::position() const {
	return _position.current();
}

rpl::producer<ElasticScrollPosition> ElasticScroll::positionValue() const {
	return _position.value();
}

ElasticScrollMovement ElasticScroll::movement() const {
	return _movement.current();
}

rpl::producer<ElasticScrollMovement> ElasticScroll::movementValue() const {
	return _movement.value();
}

int OverscrollFromAccumulated(int accumulated) {
	if (!accumulated) {
		return 0;
	}

	return (accumulated > 0 ? 1. : -1.)
		* int(base::SafeRound(RawFrom(std::abs(accumulated))));
}

int OverscrollToAccumulated(int overscroll) {
	if (!overscroll) {
		return 0;
	}
	return (overscroll > 0 ? 1. : -1.)
		* int(base::SafeRound(RawTo(std::abs(overscroll))));
}

QPointF ScrollDeltaF(not_null<QWheelEvent*> e, bool touch) {
	const auto convert = [](QPointF point) {
		return QPointF(
			style::ConvertScaleExact(point.x()),
			style::ConvertScaleExact(point.y()));
	};
	if (!e->pixelDelta().isNull()) {
		return convert(e->pixelDelta())
			* ((Platform::IsWayland() && !touch)
				? kMagicScrollMultiplier
				: 1.);
	}
	return (convert(e->angleDelta()) * QApplication::wheelScrollLines())
		/ float64(kPixelToAngleDelta * kDefaultWheelScrollLines);
}

QPoint ScrollDelta(not_null<QWheelEvent*> e, bool touch) {
	return ScrollDeltaF(e, touch).toPoint();
}

} // namespace Ui
