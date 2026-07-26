// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/kinetic_scroller.h"

#include "base/platform/base_platform_info.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QCursor>
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

QPointF KineticScroller::velocity() const {
	if (_state != Scrolling) {
		return QPointF();
	}
	const auto time = (crl::now() - _flickStarted) / 1000.;
	return _flickVelocity.pixel * std::exp(-kFriction * time);
}

void KineticScroller::handleWheelEvent(not_null<QWheelEvent*> e) {
	if (e->source() == Qt::MouseEventSynthesizedByApplication) {
		// Our own echo, or the host's touchscreen emulation.
		return;
	}
	// Kept for the synthesized events, so a Ctrl/Shift fast-scrolled
	// gesture flings fast-scrolled too.
	_modifiers = e->modifiers();
	_inverted = e->inverted();
	const auto timestamp = crl::time(e->timestamp());
	switch (e->phase()) {
	case Qt::ScrollBegin:
		press();
		break;
	case Qt::ScrollUpdate:
		if (_state == Inactive || _state == Scrolling) {
			// Momentum-race leaks and bare momentum gesture streams may
			// miss the ScrollBegin.
			press();
		}
		drag(e, timestamp);
		break;
	case Qt::ScrollEnd:
		if (_state == Dragging) {
			flick(timestamp);
		} else if (_state == Pressed) {
			setState(Inactive);
		}
		break;
	case Qt::ScrollMomentum:
		// The platform provides its own momentum stream and the host
		// applies it like any wheel input, so our kinetics aren't needed.
		if (_state != Inactive) {
			_emitted = false;
			setState(Inactive);
		}
		break;
	}
}

void KineticScroller::stop() {
	if (_state == Inactive) {
		return;
	}
	setState(Inactive);
}

void KineticScroller::press() {
	_history.clear();
	// A press during Scrolling catches the fling: the new gesture's stream
	// replaces ours, so no final ScrollEnd is emitted.
	_emitted = false;
	setState(Pressed);
}

void KineticScroller::drag(not_null<QWheelEvent*> e, crl::time timestamp) {
	while (!_history.empty()
		&& _history.front().time < timestamp - kVelocityWindow) {
		_history.erase(begin(_history));
	}
	_history.push_back({
		.time = timestamp,
		.angle = QPointF(e->angleDelta()),
		.pixel = QPointF(e->pixelDelta())
			* (::Platform::IsWayland() ? kMagicScrollMultiplier : 1.),
	});
	if (_state == Pressed) {
		// The gesture stream is synthesized from wheel events which are
		// already intentional, so no drag-start distance slop is needed.
		setState(Dragging);
	}
}

void KineticScroller::flick(crl::time timestamp) {
	const auto velocity = dragVelocity(timestamp);
	_history.clear();
	const auto throwing = std::max(
		std::abs(velocity.pixel.x()),
		std::abs(velocity.pixel.y())) / kFriction;
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
	_flickVelocity = velocity;
	_flickEmittedPixel = QPointF();
	_flickEmittedAngle = QPointF();
	// The fling is animated against crl::now() (see flickTick), but the
	// drag deltas history uses the events' own timestamps.
	_flickStarted = crl::now();
	setState(Scrolling);
}

void KineticScroller::flickTick(crl::time now) {
	const auto time = (now - _flickStarted) / 1000.;
	const auto progress = (1. - std::exp(-kFriction * time)) / kFriction;
	// Truncation carries each channel's sub-quantum remainder with the
	// correct sign, so the slow tail of the fling still progresses.
	const auto quantum = [](QPointF wanted) {
		return QPoint(
			int(std::trunc(wanted.x())),
			int(std::trunc(wanted.y())));
	};
	const auto pixel = quantum(
		_flickVelocity.pixel * progress - _flickEmittedPixel);
	const auto angle = quantum(
		_flickVelocity.angle * progress - _flickEmittedAngle);
	_flickEmittedPixel += pixel;
	_flickEmittedAngle += angle;
	if (!pixel.isNull() || !angle.isNull()) {
		_emitted = true;
		const auto weak = QPointer<KineticScroller>(this);
		sendWheel(Qt::ScrollMomentum, pixel, angle);
		if (!weak || _state != Scrolling) {
			return;
		}
	}
	const auto remaining = std::exp(-kFriction * time) / kFriction;
	const auto doneX = std::abs(_flickVelocity.pixel.x()) * remaining
		< kFlickRemainingThreshold;
	const auto doneY = std::abs(_flickVelocity.pixel.y()) * remaining
		< kFlickRemainingThreshold;
	if (doneX && doneY) {
		setState(Inactive);
	}
}

void KineticScroller::sendWheel(
		Qt::ScrollPhase phase,
		QPoint pixel,
		QPoint angle) {
	const auto global = QCursor::pos();
	auto e = QWheelEvent(
		_target->mapFromGlobal(global),
		global,
		pixel,
		angle,
		Qt::NoButton,
		_modifiers,
		phase,
		_inverted,
		Qt::MouseEventSynthesizedByApplication);
	e.setTimestamp(crl::now());
	QCoreApplication::sendEvent(_target, &e);
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
	if (object != _frameWindow || _state != Scrolling) {
		return QObject::eventFilter(object, event);
	}
	const auto type = event->type();
	if (type == QEvent::UpdateRequest) {
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
	} else if (type == QEvent::MouseMove
		|| type == QEvent::MouseButtonPress) {
		// Real pointer activity catches the fling. Mouse events reach
		// widgets through the window, so consuming here still keeps the
		// press from clicking into the still-moving content.
		const auto mouse = static_cast<QMouseEvent*>(event);
		if (type == QEvent::MouseMove) {
			if (_stopMousePos == mouse->globalPos()) {
				return false;
			}
			_stopMousePos = mouse->globalPos();
		}
		stop();
		return true;
	}
	return QObject::eventFilter(object, event);
}

KineticScroller::Velocity KineticScroller::dragVelocity(
		crl::time now) const {
	auto first = crl::time(0);
	auto last = crl::time(0);
	auto accumulated = Velocity();
	for (const auto &sample : _history) {
		if (sample.time < now - kVelocityWindow) {
			continue;
		} else if (!first) {
			first = sample.time;
		}
		last = sample.time;
		accumulated.angle += sample.angle;
		accumulated.pixel += sample.pixel;
	}
	if (first && last == first) {
		// Handle a single-event flick (empirically possible).
		last = now;
	}
	if (last <= first) {
		return {};
	}
	const auto scale = 1000. / float64(last - first);
	return {
		.angle = accumulated.angle * scale,
		.pixel = accumulated.pixel * scale,
	};
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
		_stopMousePos = QCursor::pos();
		armFrameClock();
	}
	if (state == Inactive && _emitted) {
		// Native momentum streams always finish with a ScrollEnd event,
		// so ours does too - it drives the same end-of-scroll handling
		// in the host. May destroy `this`, keep it last.
		_emitted = false;
		sendWheel(Qt::ScrollEnd, {}, {});
	}
}

} // namespace Ui
