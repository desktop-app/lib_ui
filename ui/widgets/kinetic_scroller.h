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

extern const char kOptionKineticScroller[];

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
//
// One instance per window, fed the window-level gesture stream by an
// application-wide watcher: any widget the window delivers a gesture to
// (scroll areas, input fields, any QTextEdit) gets the fling without
// per-host wiring, and a new gesture anywhere in the window catches a
// fling still running on any of its widgets. The fling target is taken
// from Qt's own wheel grab and the synthesized events climb the same
// widget chain as the real ones.
class KineticScroller final : public QObject {
public:
	enum State {
		Inactive,
		Pressed,
		Dragging,
		Scrolling,
	};

	// The window's shared scroller (created on demand as a child of the
	// widget's window handle), nullptr when the option is disabled.
	[[nodiscard]] static KineticScroller *For(not_null<QWidget*> widget);
	// The watcher's entry point, use For() from widgets.
	[[nodiscard]] static not_null<KineticScroller*> ForWindow(
		not_null<QWindow*> window);

	// Inactive unless the current gesture / fling runs inside `target`
	// (the grab is on the deepest widget under the gesture, so a scroll
	// host asks whether the stream belongs to its subtree).
	[[nodiscard]] State state(not_null<QWidget*> target) const;
	// The exact fling velocity in the stream's raw pixels per second,
	// non-zero only while Scrolling: the synthesized events truncate it to
	// whole pixels per tick, so hosts measuring it back from them would read
	// zero in the slow tail of the fling.
	[[nodiscard]] QPointF velocity() const;
	// Fed by the application-wide watcher with the window-level event,
	// before the widget delivery - hosts don't call this. Never consumes:
	// real events are applied by their receivers, synthetic ones (including
	// our own echo) are ignored here. A retarget flushes the previous
	// target's final ScrollEnd synchronously, the fling itself is paced by
	// the frame clock.
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

	explicit KineticScroller(not_null<QWindow*> window);

	void setState(State state);
	void armFrameClock();
	void disarmFrameClock();
	void press();
	[[nodiscard]] bool setTarget(not_null<QWidget*> target);
	void drag(not_null<QWheelEvent*> e, crl::time timestamp);
	void flick(crl::time timestamp);
	void flickTick(crl::time now);
	void sendWheel(Qt::ScrollPhase phase, QPoint pixel, QPoint angle);
	[[nodiscard]] Velocity dragVelocity(crl::time now) const;

	const not_null<QWindow*> _window;
	QPointer<QWidget> _target;
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
