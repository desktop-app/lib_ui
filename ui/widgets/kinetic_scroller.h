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
#include <QtCore/QRectF>

#include <vector>

class QWidget;
class QWindow;

namespace Ui {

// Reimplementation of QScroller: receives InputPress/InputMove/InputRelease
// and scrolls the target widget with QScrollPrepareEvent and QScrollEvent,
// but with the proper exponential inertia curve similar to what everyone else
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
// Never produces overshoot: ScrollArea has none, ElasticScroll does its own.
class KineticScroller final : public QObject {
	Q_OBJECT

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

	explicit KineticScroller(not_null<QWidget*> target);

	[[nodiscard]] State state() const {
		return _state;
	}
	[[nodiscard]] QPointF velocity() const;
	bool handleInput(Input input, QPointF position, crl::time timestamp);
	void stop();
	void resendPrepareEvent();

Q_SIGNALS:
	void stateChanged(State state);

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
	bool sendPrepare(QPointF position);
	void sendScroll();
	bool press(QPointF position);
	void drag(QPointF position, crl::time timestamp);
	void flick(crl::time timestamp);
	void flickTick(crl::time now);
	[[nodiscard]] QPointF flickPosition(float64 time) const;
	[[nodiscard]] QPointF dragVelocity(crl::time now) const;
	[[nodiscard]] QPointF clamped(QPointF position) const;

	const not_null<QWidget*> _target;
	QPointer<QWindow> _frameWindow;
	State _state = Inactive;
	QRectF _range;
	QPointF _contentPosition;
	QPointF _pressPosition;
	QPointF _lastPosition;
	std::vector<DragSample> _history;
	bool _scrollSent = false;

	QPointF _flickFrom;
	QPointF _flickVelocity;
	crl::time _flickStarted = 0;

};

} // namespace Ui
