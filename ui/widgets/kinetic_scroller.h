// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

#include <crl/crl_time.h>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QPointF>

#include <vector>

class QWheelEvent;
class QWidget;
class QWindow;

namespace Ui {

// Replacement for QScroller: observes the phased wheel event stream of a
// touchpad gesture to measure the release velocity, and after the release
// synthesizes a stream of Qt::ScrollMomentum wheel events (finished by a
// Qt::ScrollEnd one) with the proper exponential inertia curve similar to
// what everyone else uses (GTK, Chromium, Firefox). The stream is sent to
// the target widget, so the fling goes through exactly the same delta
// application path as the real events - and as the native momentum streams
// the platform itself provides on macOS.
//
// QScroller not only has bad inertia curve, but also initializes the fling
// with the wrong speed: its moving-average velocity estimate starts from
// zero at press and each event pulls it only ~13% closer to the true speed,
// taking ~135 ms of dragging to catch up, so the fling speed tracked
// gesture length instead of the finger speed at release.
//
// Here the release velocity is averaged over the last 150 ms of drag deltas
// and the fling decays it exponentially.
class KineticScroller final : public QObject {
public:
	enum State {
		Inactive,
		Pressed,
		Dragging,
		Scrolling,
	};

	explicit KineticScroller(not_null<QWidget*> target);

	[[nodiscard]] State state() const {
		return _state;
	}
	// The exact fling velocity in the stream's raw pixels per second,
	// non-zero only while Scrolling: the synthesized events truncate it to
	// whole pixels per tick, so hosts measuring it back from them would read
	// zero in the slow tail of the fling.
	[[nodiscard]] QPointF velocity() const;
	// Never consumes: real events are applied by the host, synthetic ones
	// (including our own echo) are ignored here. Never sends events back
	// synchronously either (the fling is paced by the frame clock), so
	// the caller can't be destroyed inside this call.
	void handleWheelEvent(not_null<QWheelEvent*> e);
	void stop();

protected:
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

	void setState(State state);
	void armFrameClock();
	void disarmFrameClock();
	void press();
	void drag(not_null<QWheelEvent*> e, crl::time timestamp);
	void flick(crl::time timestamp);
	void flickTick(crl::time now);
	void sendWheel(Qt::ScrollPhase phase, QPoint pixel, QPoint angle);
	[[nodiscard]] Velocity dragVelocity(crl::time now) const;

	const not_null<QWidget*> _target;
	QPointer<QWindow> _frameWindow;
	QPoint _stopMousePos;
	State _state = Inactive;
	std::vector<DragSample> _history;
	Qt::KeyboardModifiers _modifiers;
	bool _inverted = false;
	bool _emitted = false;

	Velocity _flickVelocity;
	QPointF _flickEmittedPixel;
	QPointF _flickEmittedAngle;
	crl::time _flickStarted = 0;

};

} // namespace Ui
