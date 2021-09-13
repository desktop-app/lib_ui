// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/path_shift_gradient.h"

#include "base/call_delayed.h"
#include "ui/effects/animations.h"

namespace Ui {
namespace {

constexpr auto kSlideDuration = crl::time(1000);
constexpr auto kWaitDuration = crl::time(1000);
constexpr auto kFullDuration = kSlideDuration + kWaitDuration;

} // namespace

struct PathShiftGradient::AnimationData {
	Ui::Animations::Simple animation;
	base::flat_set<not_null<PathShiftGradient*>> active;
	bool scheduled = false;
};

std::weak_ptr<PathShiftGradient::AnimationData> PathShiftGradient::Animation;

PathShiftGradient::PathShiftGradient(
	const style::color &bg,
	const style::color &fg,
	Fn<void()> animationCallback,
	rpl::producer<> paletteUpdated)
: _bg(bg)
, _fg(fg)
, _animationCallback(std::move(animationCallback)) {
	refreshColors();
	if (!paletteUpdated) {
		paletteUpdated = style::PaletteChanged();
	}
	std::move(
		paletteUpdated
	) | rpl::start_with_next([=] {
		refreshColors();
	}, _lifetime);
}

PathShiftGradient::~PathShiftGradient() {
	if (const auto strong = _animation.get()) {
		strong->active.erase(this);
	}
}

void PathShiftGradient::overrideColors(
		const style::color &bg,
		const style::color &fg) {
	_colorsOverriden = true;
	refreshColors(bg, fg);
}

void PathShiftGradient::clearOverridenColors() {
	if (!_colorsOverriden) {
		return;
	}
	_colorsOverriden = false;
	refreshColors();
}

void PathShiftGradient::startFrame(
		int viewportLeft,
		int viewportWidth,
		int gradientWidth) {
	_viewportLeft = viewportLeft;
	_viewportWidth = viewportWidth;
	_gradientWidth = gradientWidth;
	_geometryUpdated = false;
}

void PathShiftGradient::updateGeometry() {
	if (_geometryUpdated) {
		return;
	}
	_geometryUpdated = true;
	const auto now = crl::now();
	const auto period = now % kFullDuration;
	if (period >= kSlideDuration) {
		_gradientEnabled = false;
		return;
	}
	const auto progress = period / float64(kSlideDuration);
	_gradientStart = anim::interpolate(
		_viewportLeft - _gradientWidth,
		_viewportLeft + _viewportWidth,
		progress);
	_gradientFinalStop = _gradientStart + _gradientWidth;
	_gradientEnabled = true;
}

bool PathShiftGradient::paint(Fn<bool(const Background&)> painter) {
	updateGeometry();
	if (_gradientEnabled) {
		_gradient.setStart(_gradientStart, 0);
		_gradient.setFinalStop(_gradientFinalStop, 0);
	}
	const auto background = _gradientEnabled
		? Background(&_gradient)
		: _bgOverride
		? *_bgOverride
		: _bg;
	if (!painter(background)) {
		return false;
	}
	activateAnimation();
	return true;
}

void PathShiftGradient::activateAnimation() {
	if (_animationActive) {
		return;
	}
	_animationActive = true;
	if (!_animation) {
		_animation = Animation.lock();
		if (!_animation) {
			_animation = std::make_shared<AnimationData>();
			Animation = _animation;
		}
	}
	const auto raw = _animation.get();
	if (_animationCallback) {
		raw->active.emplace(this);
	}

	const auto globalCallback = [] {
		const auto strong = Animation.lock();
		if (!strong) {
			return;
		}
		strong->scheduled = false;
		while (!strong->active.empty()) {
			const auto entry = strong->active.back();
			strong->active.erase(strong->active.end() - 1);
			entry->_animationActive = false;
			entry->_animationCallback();
		}
	};

	const auto now = crl::now();
	const auto period = now % kFullDuration;
	if (period >= kSlideDuration) {
		const auto tillWaitFinish = kFullDuration - period;
		if (!raw->scheduled) {
			raw->scheduled = true;
			raw->animation.stop();
			base::call_delayed(tillWaitFinish, globalCallback);
		}
	} else {
		const auto tillSlideFinish = kSlideDuration - period;
		if (!raw->animation.animating()) {
			raw->animation.start(globalCallback, 0., 1., tillSlideFinish);
		}
	}
}

void PathShiftGradient::refreshColors() {
	refreshColors(_bg, _fg);
}

void PathShiftGradient::refreshColors(
		const style::color &bg,
		const style::color &fg) {
	_gradient.setStops({
		{ 0., bg->c },
		{ 0.5, fg->c },
		{ 1., bg->c },
	});
	_bgOverride = _colorsOverriden ? &bg : nullptr;
}

} // namespace Ui
