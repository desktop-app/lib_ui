// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/kinetic_scroller.h"

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

KineticScroller::KineticScroller(
	not_null<QWidget*> target,
	Fn<QPointF(QPointF)> apply)
: QObject(target)
, _target(target)
, _apply(std::move(apply)) {
}

QPointF KineticScroller::velocity() const {
	if (_state != Scrolling) {
		return QPointF();
	}
	const auto time = (crl::now() - _flickStarted) / 1000.;
	return _flickVelocity * std::exp(-kFriction * time);
}

void KineticScroller::handleInput(
		Input input,
		QPointF delta,
		crl::time timestamp) {
	switch (_state) {
	case Inactive:
		if (input == InputPress) {
			press();
		}
		return;
	case Pressed:
		if (input == InputMove) {
			drag(delta, timestamp);
		} else if (input == InputRelease) {
			setState(Inactive);
		}
		return;
	case Dragging:
		if (input == InputMove) {
			drag(delta, timestamp);
		} else if (input == InputRelease) {
			flick(timestamp);
		}
		return;
	case Scrolling:
		if (input == InputPress) {
			// No click-through: our input is a synthesized touchpad
			// gesture stream, a press should just catch the fling.
			_history.clear();
			setState(Pressed);
			setState(Dragging);
		}
		return;
	}
	Unexpected("State in KineticScroller::handleInput.");
}

void KineticScroller::stop() {
	if (_state == Inactive) {
		return;
	}
	setState(Inactive);
}

void KineticScroller::press() {
	_history.clear();
	setState(Pressed);
}

void KineticScroller::drag(QPointF delta, crl::time timestamp) {
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
	_applied = true;
	_apply(-delta);
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
	_flickEmitted = QPointF();
	_flickVelocity = velocity;
	_flickDoneX = _flickDoneY = false;
	// The fling is animated against crl::now() (see flickTick), but the
	// drag deltas history uses the events' own timestamps.
	_flickStarted = crl::now();
	setState(Scrolling);
}

void KineticScroller::flickTick(crl::time now) {
	const auto time = (now - _flickStarted) / 1000.;
	// Truncation carries the sub-pixel remainder with the correct sign, so
	// the slow tail of the fling still progresses.
	const auto wanted = flickPosition(time) - _flickEmitted;
	const auto delta = QPointF(
		_flickDoneX ? 0. : std::trunc(wanted.x()),
		_flickDoneY ? 0. : std::trunc(wanted.y()));
	_flickEmitted += delta;
	_applied = true;
	const auto weak = QPointer<KineticScroller>(this);
	const auto consumed = _apply(delta);
	if (!weak || _state != Scrolling) {
		return;
	}
	// A consumed shortfall means the fling hit the content edge on that
	// axis, finishing it there.
	if (consumed.x() != delta.x()) {
		_flickDoneX = true;
		_flickVelocity.setX(0.);
	}
	if (consumed.y() != delta.y()) {
		_flickDoneY = true;
		_flickVelocity.setY(0.);
	}
	const auto remaining = std::exp(-kFriction * time) / kFriction;
	const auto doneX = _flickDoneX
		|| (std::abs(_flickVelocity.x()) * remaining
			< kFlickRemainingThreshold);
	const auto doneY = _flickDoneY
		|| (std::abs(_flickVelocity.y()) * remaining
			< kFlickRemainingThreshold);
	if (doneX && doneY) {
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
	return _flickVelocity
		* ((1. - std::exp(-kFriction * time)) / kFriction);
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
	if (state == Inactive && _applied) {
		// Mirroring QScroller: the final notification goes out after the
		// state is already Inactive and only if any scroll happened at all.
		_applied = false;
		_apply(QPointF());
		if (!weak) {
			return;
		}
	}
	Q_EMIT stateChanged(_state);
}

} // namespace Ui
