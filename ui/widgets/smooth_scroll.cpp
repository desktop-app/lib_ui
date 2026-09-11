// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/smooth_scroll.h"

#include "ui/effects/animation_value.h"

#include <QtGui/QWheelEvent>

namespace Ui {
namespace {

// Long enough to be followed by the eye, short enough that a second notch
// on top of the first still feels like a direct answer to the wheel.
constexpr auto kDuration = crl::time(260);

[[nodiscard]] bool DiscreteWheel(not_null<QWheelEvent*> e) {
	// A classic wheel notch is the one input that arrives without a phase.
	// Modifiers are left out as well: with them a notch is multiplied to a
	// page, and a page is a jump by nature.
	if (e->phase() != Qt::NoScrollPhase) {
		return false;
	} else if (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
		return false;
	}
	const auto delta = e->angleDelta();
	return (std::abs(delta.y()) >= 120)
		&& (std::abs(delta.y()) > std::abs(delta.x()));
}

} // namespace

SmoothScroll::SmoothScroll(Fn<int()> position, Fn<void(int)> move)
: _position(std::move(position))
, _move(std::move(move)) {
	_animation.init([=](crl::time now) { step(now); });
}

void SmoothScroll::step(crl::time now) {
	// Anything that moved the scroll while we were animating it - a jump
	// to an item, the keyboard, a geometry update after more content was
	// loaded - owns the position now, and wins.
	if (_tracking && _position() != _applied) {
		cancel();
		return;
	}
	const auto progress = std::clamp(
		(now - _started) / float64(kDuration),
		0.,
		1.);
	_move(anim::interpolate(
		_from,
		_target,
		anim::easeOutQuint(1., progress)));
	_applied = _position();
	_tracking = true;
	if (progress >= 1.) {
		cancel();
	}
}

bool SmoothScroll::wheelEvent(
		not_null<QWheelEvent*> e,
		int delta,
		int minimum,
		int maximum) {
	// The angle delta already points where the user wants to go: a
	// natural-scrolling setting is applied to it before it reaches us,
	// and inverted() only reports that the setting is on.
	if (anim::Disabled() || !DiscreteWheel(e) || !delta) {
		cancel();
		return false;
	}
	if (minimum >= maximum) {
		cancel();
		return false;
	}
	const auto animating = _animation.animating();
	const auto base = animating ? _target : _position();
	const auto target = base + delta;
	if (target < minimum || target > maximum) {
		// The edges belong to the scroll itself: that is where the
		// elastic overscroll is stretched and where a list is asked for
		// more content to append below the last item.
		if (animating) {
			// A flight in progress leaves the scroll on an intermediate
			// frame, so give it the position that flight was travelling
			// to. The notch is handed back to the scroll below and would
			// otherwise start from that frame and stop short of the edge.
			_move(base);
		}
		cancel();
		return false;
	}
	_from = _position();
	_target = target;
	_started = crl::now();
	_tracking = false;
	if (!animating) {
		_animation.start();
	}
	return true;
}

void SmoothScroll::cancel() {
	_animation.stop();
	_tracking = false;
}

} // namespace Ui
