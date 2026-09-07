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

class SmoothScroll final {
public:
	SmoothScroll(Fn<int()> position, Fn<void(int)> move);

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
