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

class QWidget;
class QWindow;

namespace Ui {

// Replacement for QScroller: receives InputPress/InputMove/InputRelease with
// wheel deltas and scrolls the target widget through the apply callback,
// with the proper exponential inertia curve similar to what everyone else
// uses (GTK, Chromium, Firefox).
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
// The apply callback receives integral content-space deltas and returns the
// consumed part: a shortfall means the content edge was hit, finishing the
// fling on that axis.
class KineticScroller final : public QObject {
public:
	enum State {
		Inactive,
		Pressed,
		Dragging,
		Scrolling,
	};
	enum Input {
		InputPress,
		InputMove,
		InputRelease,
	};

	KineticScroller(not_null<QWidget*> target, Fn<QPointF(QPointF)> apply);

	[[nodiscard]] State state() const {
		return _state;
	}
	[[nodiscard]] QPointF velocity() const;
	// Wheel-space delta, ignored for press/release. The timestamp must be
	// the event's own.
	void handleInput(Input input, QPointF delta, crl::time timestamp);
	void stop();

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	struct DragSample {
		crl::time time = 0;
		QPointF delta;
	};

	void setState(State state);
	void armFrameClock();
	void disarmFrameClock();
	void press();
	void drag(QPointF delta, crl::time timestamp);
	void flick(crl::time timestamp);
	void flickTick(crl::time now);
	[[nodiscard]] QPointF flickPosition(float64 time) const;
	[[nodiscard]] QPointF dragVelocity(crl::time now) const;

	const not_null<QWidget*> _target;
	const Fn<QPointF(QPointF)> _apply;
	QPointer<QWindow> _frameWindow;
	QPoint _stopMousePos;
	State _state = Inactive;
	std::vector<DragSample> _history;
	bool _applied = false;

	QPointF _flickEmitted;
	QPointF _flickVelocity;
	crl::time _flickStarted = 0;
	bool _flickDoneX = false;
	bool _flickDoneY = false;

};

} // namespace Ui
