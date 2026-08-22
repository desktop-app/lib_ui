// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/kinetic_scroller.h"

#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QCursor>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <private/qapplication_p.h>

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

// A substantial synthesized momentum tick that no widget in the chain
// accepted means the fling ran into an end of range nothing can consume;
// smaller ticks are exempt, scrollbars ignore them while accumulating.
constexpr auto kFlickUnconsumedPixelThreshold = 8;
constexpr auto kFlickUnconsumedAngleThreshold = 60;

const auto kKineticScrollerObjectName = u"_td_kinetic_scroller"_q;

base::options::toggle OptionKineticScroller({
	.id = kOptionKineticScroller,
	.name = "Use KineticScroller for touchpad scrolling",
	.description = "Provides kinetic scrolling",
	.defaultValue = Platform::IsLinux(),
});

// Observes the phased wheel event streams at the window level, before the
// widget delivery, and hands them to the windows' own scrollers: this way
// any widget a window delivers a gesture to gets the kinetics, without
// per-host wiring.
class StreamWatcher final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		if (event->type() != QEvent::Wheel || !object->isWindowType()) {
			return false;
		}
		const auto e = static_cast<QWheelEvent*>(event);
		if (e->phase() == Qt::NoScrollPhase
			|| e->source() == Qt::MouseEventSynthesizedByApplication
			|| !OptionKineticScroller.value()) {
			return false;
		}
		// QApplication::notify swallows wheel events of other windows
		// while a popup is open, without updating the wheel grab, so
		// observing them would fling into a stale target.
		if (const auto popup = QApplication::activePopupWidget()) {
			if (popup->window()->windowHandle() != object) {
				return false;
			}
		}
		const auto window = static_cast<QWindow*>(object);
		KineticScroller::ForWindow(window)->handleWheelEvent(e);
		return false;
	}

};

void InstallStreamWatcher() {
	const auto app = QCoreApplication::instance();
	app->installEventFilter(new StreamWatcher(app));
}

Q_COREAPP_STARTUP_FUNCTION(InstallStreamWatcher)

} // namespace

const char kOptionKineticScroller[] = "kinetic-scroller";

KineticScroller *KineticScroller::For(not_null<QWidget*> widget) {
	if (!OptionKineticScroller.value()) {
		return nullptr;
	}
	const auto window = widget->window()->windowHandle();
	return window ? ForWindow(window).get() : nullptr;
}

not_null<KineticScroller*> KineticScroller::ForWindow(
		not_null<QWindow*> window) {
	// No Q_OBJECT here, so a typed findChild can't match - look the
	// instance up by the object name, like the platform helpers do.
	if (const auto found = window->findChild<QObject*>(
			kKineticScrollerObjectName,
			Qt::FindDirectChildrenOnly)) {
		return static_cast<KineticScroller*>(found);
	}
	return new KineticScroller(window);
}

KineticScroller::KineticScroller(not_null<QWindow*> window)
: QObject(window)
, _window(window) {
	setObjectName(kKineticScrollerObjectName);
}

KineticScroller::State KineticScroller::state(
		not_null<QWidget*> target) const {
	// The grab is on the deepest widget under the gesture, so the host
	// asks whether the stream runs inside its subtree.
	const auto grabbed = _target.data();
	return (grabbed && target->isAncestorOf(grabbed)) ? _state : Inactive;
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
		// Our own echo, or a host's touchscreen emulation.
		return;
	}
	const auto phase = e->phase();
	if (phase == Qt::ScrollBegin || phase == Qt::ScrollUpdate) {
		// Kept for the synthesized events, so a Ctrl/Shift fast-scrolled
		// gesture flings fast-scrolled too. Not taken from end / momentum
		// events: those may finish another target's stale stream and
		// shouldn't repaint an in-flight fling's modifiers.
		_modifiers = e->modifiers();
		_inverted = e->inverted();
	}
	const auto timestamp = crl::time(e->timestamp());
	switch (phase) {
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
		// The platform provides its own momentum stream and the receivers
		// apply it like any wheel input, so our kinetics aren't needed.
		if (_state != Inactive) {
			if (_target && _target == QApplicationPrivate::wheel_widget) {
				// The native stream replaces ours on the same target and
				// finishes with its own ScrollEnd.
				_emitted = false;
			}
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
	// A press during Scrolling catches the fling, but the target of the
	// new gesture is unknown until its events start flowing: whether the
	// caught fling was replaced by the new stream on the same target (no
	// final ScrollEnd then) or must be flushed is decided by the first
	// drag's setTarget.
	setState(Pressed);
}

bool KineticScroller::setTarget(not_null<QWidget*> target) {
	if (_target == target.get()) {
		// The gesture continues (or caught a fling) on the same target,
		// so its stream seamlessly replaces ours: no final ScrollEnd.
		_emitted = false;
		return true;
	}
	if (_emitted) {
		// Retargeting must not leave the old target's momentum stream
		// dangling: flush its final ScrollEnd first. May destroy `this`.
		_emitted = false;
		const auto weak = QPointer<KineticScroller>(this);
		sendWheel(Qt::ScrollEnd, {}, {});
		if (!weak) {
			return false;
		}
	}
	_target = target.get();
	return true;
}

void KineticScroller::drag(not_null<QWheelEvent*> e, crl::time timestamp) {
	// Qt pins the phased stream to the deepest widget under the gesture
	// start, and has already updated the grab while delivering the events
	// observed before this one, so by the first drag it identifies the
	// fling target. Left untouched if the stream doesn't feed the grab
	// (e.g. it belongs to a non-widget window): then there is no target
	// and no fling.
	const auto grabbed = QApplicationPrivate::wheel_widget.data();
	if (grabbed && !setTarget(grabbed)) {
		return;
	}
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
	if (!_target || _target->window()->windowHandle() != _window) {
		// The target died or left for another window during the gesture:
		// the fling isn't ours to run.
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
	if (!_target) {
		// The fling's target died: nobody to send the final ScrollEnd to.
		_emitted = false;
		setState(Inactive);
		return;
	} else if (_target->window()->windowHandle() != _window) {
		// The target left for another window mid-fling: not ours anymore,
		// but alive - finish its stream.
		setState(Inactive);
		return;
	}
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
	// Synthesized wheel events are delivered to their exact receiver only
	// (QApplication::notify propagates just the spontaneous ones), so climb
	// the same widget chain the real gesture events did.
	const auto global = QCursor::pos();
	const auto weak = QPointer<KineticScroller>(this);
	const auto was = _state;
	auto target = _target.data();
	auto accepted = false;
	while (target) {
		auto e = QWheelEvent(
			target->mapFromGlobal(global),
			global,
			pixel,
			angle,
			Qt::NoButton,
			_modifiers,
			phase,
			_inverted,
			Qt::MouseEventSynthesizedByApplication);
		e.setTimestamp(crl::now());
		const auto handled = QCoreApplication::sendEvent(target, &e);
		if (!weak || _state != was) {
			// A receiver stopped or restarted us: the rest of the tick
			// isn't ours to deliver.
			return;
		} else if (handled && e.isAccepted()) {
			accepted = true;
			break;
		} else if (target->isWindow()
			|| target->testAttribute(Qt::WA_NoMousePropagation)) {
			break;
		}
		target = target->parentWidget();
	}
	if (phase == Qt::ScrollMomentum
		&& !accepted
		&& (std::max(std::abs(pixel.x()), std::abs(pixel.y()))
				>= kFlickUnconsumedPixelThreshold
			|| std::max(std::abs(angle.x()), std::abs(angle.y()))
				>= kFlickUnconsumedAngleThreshold)) {
		// A substantial tick nobody could use: the fling ran into an end
		// of range nothing can consume (e.g. inside a fully scrolled input
		// field), finish early instead of letting the mouse-catch filter
		// eat clicks for the rest of the decay.
		setState(Inactive);
	}
}

void KineticScroller::armFrameClock() {
	// UpdateRequest is delivered in sync with the compositor frame clock
	// where the platform provides one (Wayland frame callbacks,
	// CVDisplayLink); elsewhere Qt itself paces it with a ~5 ms timer.
	_window->installEventFilter(this);
	_window->requestUpdate();
}

void KineticScroller::disarmFrameClock() {
	_window->removeEventFilter(this);
}

bool KineticScroller::eventFilter(QObject *object, QEvent *event) {
	if (object != _window || _state != Scrolling) {
		return QObject::eventFilter(object, event);
	}
	const auto type = event->type();
	if (type == QEvent::UpdateRequest) {
		const auto weak = QPointer<KineticScroller>(this);
		flickTick(crl::now());
		if (weak && _state == Scrolling) {
			_window->requestUpdate();
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
		if (_target) {
			sendWheel(Qt::ScrollEnd, {}, {});
		}
	}
}

} // namespace Ui
