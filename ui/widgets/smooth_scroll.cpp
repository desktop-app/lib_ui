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

constexpr auto kDuration = crl::time(260);

[[nodiscard]] bool DiscreteWheel(not_null<QWheelEvent*> e) {
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
	if (!DiscreteWheel(e) || !delta) {
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
		if (animating) {
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
