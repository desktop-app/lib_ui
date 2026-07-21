// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/kinetic_scroller.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>
#include <QtWidgets/QWidget>

#include <cmath>

namespace Ui {
namespace {

// Velocity decays as v0 * exp(-kFriction * t): ~170 ms half-life, the
// total throw is v0 / kFriction - linear in the release velocity.
constexpr auto kFriction = 4.; // 1 / s

// Velocity is measured over the trailing window of drag deltas, so it
// reflects the swiftness of the gesture tail no matter how long the whole
// gesture was.
constexpr auto kVelocityWindow = crl::time(150);

// Finish the fling once the remaining throw can't move a pixel.
constexpr auto kFlickRemainingThreshold = 0.5;

} // namespace

KineticScroller::KineticScroller(not_null<QWidget*> target)
: QObject(target)
, _target(target) {
}

bool KineticScroller::handleInput(
		Input input,
		QPointF position,
		crl::time timestamp) {
	switch (_state) {
	case Inactive:
		return (input == InputPress) && press(position);
	case Pressed:
		if (input == InputMove) {
			drag(position, timestamp);
			return true;
		} else if (input == InputRelease) {
			setState(Inactive);
		}
		return false;
	case Dragging:
		if (input == InputMove) {
			drag(position, timestamp);
			return true;
		} else if (input == InputRelease) {
			flick(timestamp);
			return true;
		}
		return false;
	case Scrolling:
		if (input == InputPress) {
			// No click-through: our input is a synthesized touchpad
			// gesture stream, a press should just catch the fling.
			_pressPosition = _lastPosition = position;
			_history.clear();
			setState(Pressed);
			setState(Dragging);
			return true;
		}
		return false;
	}
	Unexpected("State in KineticScroller::handleInput.");
}

void KineticScroller::stop() {
	if (_state == Inactive) {
		return;
	}
	_contentPosition = clamped(_contentPosition);
	setState(Inactive);
}

void KineticScroller::resendPrepareEvent() {
	sendPrepare(_pressPosition);
}

bool KineticScroller::press(QPointF position) {
	if (!sendPrepare(position)) {
		return false;
	}
	_pressPosition = _lastPosition = position;
	_history.clear();
	setState(Pressed);
	return true;
}

void KineticScroller::drag(QPointF position, crl::time timestamp) {
	const auto delta = position - _lastPosition;
	_lastPosition = position;
	while (!_history.empty()
		&& _history.front().time < timestamp - kVelocityWindow) {
		_history.erase(begin(_history));
	}
	_history.push_back({ timestamp, delta });
	if (_state == Pressed) {
		// The gesture stream is synthesized from wheel events which are
		// already intentional, so no drag-start distance slop is needed.
		setState(Dragging);
	}
	_contentPosition = clamped(_contentPosition - delta);
	sendScroll();
}

void KineticScroller::flick(crl::time timestamp) {
	const auto velocity = dragVelocity(timestamp);
	_history.clear();
	const auto throwing = std::max(
		std::abs(velocity.x()),
		std::abs(velocity.y())) / kFriction;
	if (throwing < kFlickRemainingThreshold) {
		setState(Inactive);
		return;
	}
	const auto window = _target->window()->windowHandle();
	if (!window || !window->handle()) {
		// Unreachable in practice (we have a window by construction).
		setState(Inactive);
		return;
	}
	_flickFrom = _contentPosition;
	_flickVelocity = velocity;
	_flickStarted = timestamp;
	setState(Scrolling);
}

void KineticScroller::flickTick(crl::time now) {
	_contentPosition = clamped(
		flickPosition((now - _flickStarted) / 1000.));
	const auto weak = QPointer<KineticScroller>(this);
	sendScroll();
	if (!weak || _state != Scrolling) {
		return;
	}
	// The widget could have handled the event by extending the range
	// through resendPrepareEvent (which rebases the fling), so evaluate
	// the trajectory only now, against the possibly updated state.
	const auto time = (now - _flickStarted) / 1000.;
	const auto position = flickPosition(time);
	const auto remaining = std::exp(-kFriction * time) / kFriction;
	const auto doneX = (std::abs(_flickVelocity.x()) * remaining
			< kFlickRemainingThreshold)
		|| (position.x() != std::clamp(
			position.x(),
			_range.left(),
			_range.right()));
	const auto doneY = (std::abs(_flickVelocity.y()) * remaining
			< kFlickRemainingThreshold)
		|| (position.y() != std::clamp(
			position.y(),
			_range.top(),
			_range.bottom()));
	if (doneX && doneY) {
		_contentPosition = clamped(position);
		setState(Inactive);
	}
}

void KineticScroller::armFrameClock() {
	// UpdateRequest is delivered in sync with the compositor frame clock
	// where the platform provides one (Wayland frame callbacks,
	// CVDisplayLink); elsewhere Qt itself paces it with a ~5 ms timer.
	const auto window = _target->window()->windowHandle();
	if (_frameWindow && _frameWindow != window) {
		_frameWindow->removeEventFilter(this);
	}
	_frameWindow = window;
	_frameWindow->installEventFilter(this);
	_frameWindow->requestUpdate();
}

void KineticScroller::disarmFrameClock() {
	if (_frameWindow) {
		_frameWindow->removeEventFilter(this);
	}
}

bool KineticScroller::eventFilter(QObject *object, QEvent *event) {
	if (object == _frameWindow
		&& event->type() == QEvent::UpdateRequest
		&& _state == Scrolling) {
		const auto weak = QPointer<KineticScroller>(this);
		flickTick(crl::now());
		if (weak && _state == Scrolling) {
			_frameWindow->requestUpdate();
		}
		// Consume: QWidgetWindow answers a window-level UpdateRequest
		// with an unconditional full top-level repaint(). The scroll
		// already invalidates exactly what it dirtied, and the widget
		// repaint pipeline delivers its update requests to the widgets
		// themselves, never through the window, so nothing is starved.
		return true;
	}
	return QObject::eventFilter(object, event);
}

QPointF KineticScroller::flickPosition(float64 time) const {
	return _flickFrom
		+ _flickVelocity * ((1. - std::exp(-kFriction * time)) / kFriction);
}

QPointF KineticScroller::dragVelocity(crl::time now) const {
	auto first = crl::time(0);
	auto last = crl::time(0);
	auto accumulated = QPointF();
	for (const auto &sample : _history) {
		if (sample.time < now - kVelocityWindow) {
			continue;
		} else if (!first) {
			first = sample.time;
		}
		last = sample.time;
		accumulated += sample.delta;
	}
	if (first && last == first) {
		// Handle a single-event flick (empirically possible).
		last = now;
	}
	return (last > first)
		? -accumulated * 1000. / float64(last - first)
		: QPointF();
}

QPointF KineticScroller::clamped(QPointF position) const {
	return QPointF(
		std::clamp(position.x(), _range.left(), _range.right()),
		std::clamp(position.y(), _range.top(), _range.bottom()));
}

bool KineticScroller::sendPrepare(QPointF position) {
	auto prepare = QScrollPrepareEvent(position);
	prepare.ignore();
	const auto weak = QPointer<KineticScroller>(this);
	QCoreApplication::sendEvent(_target, &prepare);
	if (!weak || !prepare.isAccepted()) {
		return false;
	}
	auto range = prepare.contentPosRange();
	if (range.width() < 0.) {
		range.setWidth(0.);
	}
	if (range.height() < 0.) {
		range.setHeight(0.);
	}
	_range = range;
	const auto reported = clamped(prepare.contentPos());
	if (_state == Scrolling) {
		// Continue the fling from the freshly reported position with
		// the velocity it has decayed to by now.
		const auto now = crl::now();
		_flickVelocity *= std::exp(
			-kFriction * (now - _flickStarted) / 1000.);
		_flickFrom = reported;
		_flickStarted = now;
	}
	_contentPosition = reported;
	return true;
}

void KineticScroller::sendScroll() {
	auto event = QScrollEvent(
		_contentPosition,
		QPointF(),
		(_scrollSent
			? QScrollEvent::ScrollUpdated
			: QScrollEvent::ScrollStarted));
	_scrollSent = true;
	QCoreApplication::sendEvent(_target, &event);
}

void KineticScroller::setState(State state) {
	if (_state == state) {
		return;
	}
	if (state == Inactive || state == Pressed) {
		disarmFrameClock();
	}
	_state = state;
	if (state == Scrolling) {
		armFrameClock();
	}
	const auto weak = QPointer<KineticScroller>(this);
	if (state == Inactive && _scrollSent) {
		// Mirroring QScroller: the final event goes out after the state
		// is already Inactive and only if any scroll happened at all.
		_scrollSent = false;
		auto event = QScrollEvent(
			_contentPosition,
			QPointF(),
			QScrollEvent::ScrollFinished);
		QCoreApplication::sendEvent(_target, &event);
		if (!weak) {
			return;
		}
	}
	Q_EMIT stateChanged(_state);
}

} // namespace Ui
