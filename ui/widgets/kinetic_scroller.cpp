// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/kinetic_scroller.h"

#include "base/platform/base_platform_info.h"
#include "base/options.h"
#include "base/unique_qptr.h"
#include "ui/ui_utility.h"

#include <crl/crl_time.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QPointingDevice>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>
#include <private/qguiapplication_p.h>
#include <qpa/qwindowsysteminterface_p.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Ui {

const char kOptionKineticScroller[] = "kinetic-scroller";

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

base::options::toggle OptionKineticScroller({
	.id = kOptionKineticScroller,
	.name = "Custom kinetic scrolling for touchpads",
	.defaultValue = Platform::IsLinux(),
});

// Replacement for QScroller: observes the phased wheel event stream of a
// touchpad gesture to measure the release velocity, and after the release
// synthesizes a stream of Qt::ScrollMomentum wheel events (finished by a
// Qt::ScrollEnd one) with the proper exponential inertia curve similar to
// what everyone else uses (GTK, Chromium, Firefox). The stream re-enters
// Qt's window-system pipeline, so it follows the same targeting,
// propagation, popup, modal, and wheel-grab paths as native momentum.
//
// QScroller not only has bad inertia curve, but also initializes the fling
// with the wrong speed: its moving-average velocity estimate starts from
// zero at press and each event pulls it only ~13% closer to the true speed,
// taking ~135 ms of dragging to catch up, so the fling speed tracked
// gesture length instead of the finger speed at release.
//
// Here the release velocity is averaged over the last 150 ms of drag deltas
// and the fling decays it exponentially.
//
// One application-owned instance follows Qt's process-global wheel grab.
class KineticScroller final : public QObject {
public:
	using QObject::QObject;

	enum State {
		Inactive,
		Dragging,
		Scrolling,
	};

	bool eventFilter(QObject *object, QEvent *event) override;

private:
	// The angle channel is echoed raw, so stock Qt wheel handling applies
	// the fling at exactly the drag's rate. The pixel channel is raw too,
	// with only the Wayland multiplier pre-applied: ScrollDeltaF skips it
	// for the synthesized echo, but converts the rest uniformly.
	struct DragSample {
		crl::time time = 0;
		QPointF angle;
		QPointF pixel;
	};

	struct Velocity {
		QPointF angle;
		QPointF pixel;
	};

	[[nodiscard]] bool handleWheelEvent(
		not_null<QWindow*> window,
		not_null<QWheelEvent*> e);
	void rememberInput(not_null<QWheelEvent*> e);
	void interrupt(bool force);
	[[nodiscard]] bool start(
		not_null<QWindow*> window,
		not_null<QWheelEvent*> e);
	void end(bool force);
	void drag(not_null<QWheelEvent*> e, crl::time timestamp);
	[[nodiscard]] bool flick(crl::time timestamp);
	void flickTick(crl::time now);
	bool sendWheel(Qt::ScrollPhase phase, QPoint pixel, QPoint angle);
	[[nodiscard]] Velocity dragVelocity(crl::time now) const;

	QPointer<QWindow> _window;
	QPointer<const QPointingDevice> _device;
	QPoint _stopMousePos;
	State _state = Inactive;
	uint64 _generation = 0;
	std::vector<DragSample> _history;
	Qt::KeyboardModifiers _modifiers;
	bool _inverted = false;
	ulong _inputTimestamp = 0;
	crl::time _inputReceivedAt = 0;

	Velocity _flickVelocity;
	QPointF _flickEmittedPixel;
	QPointF _flickEmittedAngle;
	crl::time _flickStarted = 0;

};

bool KineticScroller::handleWheelEvent(
		not_null<QWindow*> window,
		not_null<QWheelEvent*> e) {
	const auto duplicate = (_window == window)
		&& (e->timestamp() == _inputTimestamp)
		&& e->pixelDelta().isNull()
		&& e->angleDelta().isNull();
	switch (e->phase()) {
	case Qt::ScrollBegin:
		if (_state == Dragging
			&& _history.empty()
			&& duplicate) {
			rememberInput(e);
		} else if (!start(window, e)) {
			return false;
		}
		break;
	case Qt::ScrollUpdate:
		if (_window != window
			|| _state == Inactive
			|| _state == Scrolling) {
			if (!start(window, e)) {
				return false;
			}
		} else {
			rememberInput(e);
		}
		drag(e, crl::time(e->timestamp()));
		break;
	case Qt::ScrollEnd:
		if (_window != window) {
			_state = Inactive;
			break;
		} else if (_state == Dragging) {
			return flick(crl::time(e->timestamp()));
		} else if (_state == Scrolling) {
			if (duplicate) {
				return true;
			}
			_state = Inactive;
		}
		break;
	case Qt::ScrollMomentum:
		if (_state != Inactive) {
			if (_window == window) {
				_state = Inactive;
			} else {
				interrupt(true);
			}
		}
		break;
	case Qt::NoScrollPhase:
		break;
	}
	return false;
}

void KineticScroller::rememberInput(not_null<QWheelEvent*> e) {
	_device = e->pointingDevice();
	_modifiers = e->modifiers();
	_inverted = e->inverted();
	_inputTimestamp = ulong(e->timestamp());
	_inputReceivedAt = crl::now();
}

void KineticScroller::interrupt(bool force) {
	if (_state == Inactive) {
		return;
	}
	const auto generation = _generation;
	const auto previous = _modifiers;
	const auto current = QGuiApplicationPrivate::modifier_buttons;
	end(force);
	if (generation == _generation
		&& QGuiApplicationPrivate::modifier_buttons == previous) {
		QGuiApplicationPrivate::modifier_buttons = current;
	}
}

bool KineticScroller::start(
		not_null<QWindow*> window,
		not_null<QWheelEvent*> e) {
	const auto generation = _generation;
	const auto guardedWindow = QPointer<QWindow>(window.get());
	interrupt(true);
	if (!guardedWindow || generation != _generation) {
		return false;
	}
	_window = guardedWindow;
	rememberInput(e);
	_history.clear();
	_state = Dragging;
	return true;
}

void KineticScroller::end(bool force) {
	if (_state == Inactive) {
		return;
	}
	const auto send = force || (_state == Scrolling);
	_state = Inactive;
	if (send) {
		sendWheel(Qt::ScrollEnd, {}, {});
	}
}

void KineticScroller::drag(
		not_null<QWheelEvent*> e,
		crl::time timestamp) {
	if (e->pixelDelta().isNull() && e->angleDelta().isNull()) {
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
}

bool KineticScroller::flick(crl::time timestamp) {
	_inputTimestamp = ulong(timestamp);
	_inputReceivedAt = crl::now();
	const auto velocity = dragVelocity(timestamp);
	_history.clear();
	const auto throwing = std::max(
		std::abs(velocity.pixel.x()),
		std::abs(velocity.pixel.y())) / kFriction;
	if (throwing < kFlickRemainingThreshold) {
		_state = Inactive;
		return false;
	}
	_flickVelocity = velocity;
	_flickEmittedPixel = QPointF();
	_flickEmittedAngle = QPointF();
	_flickStarted = crl::now();
	_state = Scrolling;
	_stopMousePos = QCursor::pos();
	_window->requestUpdate();
	return true;
}

void KineticScroller::flickTick(crl::time now) {
	if (!_window) {
		_state = Inactive;
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
		if (!sendWheel(
			Qt::ScrollMomentum,
			pixel,
			angle)) {
			return;
		}
	}
	const auto remaining = std::exp(-kFriction * time) / kFriction;
	const auto remainingThrow = std::max(
		std::abs(_flickVelocity.pixel.x()),
		std::abs(_flickVelocity.pixel.y())) * remaining;
	if (remainingThrow < kFlickRemainingThreshold) {
		end(false);
	}
}

bool KineticScroller::sendWheel(
		Qt::ScrollPhase phase,
		QPoint pixel,
		QPoint angle) {
	const auto device = _device
		? _device.data()
		: QPointingDevice::primaryPointingDevice();
	const auto window = _window;
	if (!window) {
		_state = Inactive;
		return false;
	}
	const auto globalPosition = QPointF(QCursor::pos());
	const auto position = window->mapFromGlobal(globalPosition);
	const auto generation = _generation;
	const auto was = _state;
	const auto horizontal = !angle.y() && angle.x();
	auto event = QWindowSystemInterfacePrivate::WheelEvent(
		window.data(),
		_inputTimestamp + ulong(std::max(
			crl::time(0),
			crl::now() - _inputReceivedAt)),
		position,
		globalPosition,
		pixel,
		angle,
		horizontal ? angle.x() : angle.y(),
		horizontal ? Qt::Horizontal : Qt::Vertical,
		_modifiers,
		phase,
		Qt::MouseEventSynthesizedByApplication,
		_inverted,
		device);
	if (const auto handler = QWindowSystemInterfacePrivate::eventHandler) {
		handler->sendEvent(&event);
	} else {
		QGuiApplicationPrivate::processWindowSystemEvent(&event);
	}
	if (_state != was
		|| generation != _generation) {
		return false;
	}
	if (!window) {
		_state = Inactive;
		return false;
	}
	return true;
}

bool KineticScroller::eventFilter(
		QObject *object,
		QEvent *event) {
	const auto type = event->type();
	if (type != QEvent::Wheel
		&& event->isInputEvent()
		&& event->spontaneous()) {
		++_generation;
	}
	if (type == QEvent::Wheel) {
		if (!event->spontaneous()
			|| !object->isWindowType()
			|| !object->inherits("QWidgetWindow")) {
			return false;
		}
		const auto window = static_cast<QWindow*>(object);
		const auto e = static_cast<QWheelEvent*>(event);
		const auto guardedWindow = QPointer<QWindow>(window);
		if (e->phase() == Qt::NoScrollPhase) {
			const auto generation = ++_generation;
			interrupt(true);
			return !guardedWindow || generation != _generation;
		} else if (e->source() == Qt::MouseEventSynthesizedByApplication) {
			return false;
		}
		const auto generation = ++_generation;
		const auto consume = handleWheelEvent(window, e);
		return consume
			|| !guardedWindow
			|| generation != _generation;
	}
	if (object != _window || _state != Scrolling) {
		return false;
	}
	if (type == QEvent::UpdateRequest) {
		const auto window = _window;
		flickTick(crl::now());
		if (_state == Scrolling && _window == window && window) {
			window->requestUpdate();
		}
		// Consume: QWidgetWindow answers a window-level UpdateRequest
		// with an unconditional full top-level repaint(). The scroll
		// already invalidates exactly what it dirtied, and the widget
		// repaint pipeline delivers its update requests to the widgets
		// themselves, never through the window, so nothing is starved.
		return true;
	}
	if (type == QEvent::MouseMove
		|| type == QEvent::MouseButtonPress) {
		if (!event->spontaneous()) {
			return false;
		}
		const auto mouse = static_cast<QMouseEvent*>(event);
		if (type == QEvent::MouseMove) {
			if (_stopMousePos == mouse->globalPos()) {
				return false;
			}
			_stopMousePos = mouse->globalPos();
		}
		const auto generation = _generation;
		const auto guardedWindow = _window;
		interrupt(false);
		return !guardedWindow || generation != _generation;
	}
	return false;
}

KineticScroller::Velocity KineticScroller::dragVelocity(
		crl::time now) const {
	const auto first = std::find_if(
		_history.begin(),
		_history.end(),
		[&](const auto &sample) {
			return sample.time >= now - kVelocityWindow;
		});
	if (first == _history.end() || now <= first->time) {
		return {};
	}
	auto from = first + 1;
	if (from == _history.end()) {
		from = first;
	}
	auto accumulated = Velocity();
	for (auto i = from; i != _history.end(); ++i) {
		accumulated.angle += i->angle;
		accumulated.pixel += i->pixel;
	}
	const auto scale = 1000. / float64(now - first->time);
	return {
		.angle = accumulated.angle * scale,
		.pixel = accumulated.pixel * scale,
	};
}

void InstallKineticScroller() {
	static const auto lifetime = rpl::single(
		rpl::empty
	) | rpl::then(
		OptionKineticScroller.changes()
	) | rpl::on_next([] {
		static auto Scroller = base::unique_qptr<KineticScroller>();
		Scroller = OptionKineticScroller.value()
			? base::make_unique_q<KineticScroller>(qApp)
			: nullptr;
		if (!Scroller) {
			return;
		}
		qApp->installEventFilter(Scroller.get());
	});
}

Q_COREAPP_STARTUP_FUNCTION(InstallKineticScroller)

} // namespace

} // namespace Ui
