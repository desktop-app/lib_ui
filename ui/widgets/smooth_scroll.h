// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animations.h"

class QWheelEvent;

namespace Ui {

// A wheel notch moves a scroll straight to its new position, so reading a
// long list with the wheel is a series of jumps with nothing to follow
// between them. This eases each notch to that same position instead.
//
// A scroll owns one of these and offers it every wheel event just before
// applying the delta itself. Only a classic notch is taken over: a
// trackpad, a touch screen and the momentum tail after them already
// arrive as a smooth stream of phased deltas, and animating on top of one
// would only make it worse.
class SmoothScroll final {
public:
	SmoothScroll(Fn<int()> position, Fn<void(int)> move);

	// `delta` is the position change the scroll was about to apply at
	// once, `minimum` and `maximum` the range it may travel in. Returns
	// true when the notch was taken over, and the caller applies
	// nothing: the animation owns the position from now on.
	[[nodiscard]] bool wheelEvent(
		not_null<QWheelEvent*> e,
		int delta,
		int minimum,
		int maximum);

	void cancel();

private:
	void step(crl::time now);

	const Fn<int()> _position;
	const Fn<void(int)> _move;

	Animations::Basic _animation;
	crl::time _started = 0;
	int _from = 0;
	int _target = 0;
	int _applied = 0;
	bool _tracking = false;

};

} // namespace Ui
