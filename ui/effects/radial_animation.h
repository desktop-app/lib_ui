// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animations.h"

namespace style {
struct InfiniteRadialAnimation;
} // namespace style

namespace Ui {

struct RadialState {
	static const int kFull;

	float64 shown = 0.;
	int arcFrom = 0;
	int arcLength = kFull;
};

class RadialAnimation {
public:
	template <typename Callback>
	RadialAnimation(Callback &&callback);

	[[nodiscard]] float64 opacity() const {
		return _opacity;
	}
	[[nodiscard]] bool animating() const {
		return _animation.animating();
	}

	void start(float64 prg);
	bool update(float64 prg, bool finished, crl::time ms);
	void stop();

	void draw(
		QPainter &p,
		const QRect &inner,
		int32 thickness,
		style::color color) const;

	[[nodiscard]] RadialState computeState() const;

private:
	crl::time _firstStart = 0;
	crl::time _lastStart = 0;
	crl::time _lastTime = 0;
	float64 _opacity = 0.;
	anim::value _arcEnd;
	anim::value _arcStart;
	Ui::Animations::Basic _animation;
	bool _finished = false;

};

template <typename Callback>
inline RadialAnimation::RadialAnimation(Callback &&callback)
: _arcStart(0, RadialState::kFull)
, _animation(std::forward<Callback>(callback)) {
}


class InfiniteRadialAnimation {
public:
	template <typename Callback>
	InfiniteRadialAnimation(
		Callback &&callback,
		const style::InfiniteRadialAnimation &st);

	[[nodiscard]] bool animating() const {
		return _animation.animating();
	}

	void start(crl::time skip = 0);
	void stop(anim::type animated = anim::type::normal);

	void draw(
		QPainter &p,
		QPoint position,
		int outerWidth);
	void draw(
		QPainter &p,
		QPoint position,
		QSize size,
		int outerWidth);

	static void Draw(
		QPainter &p,
		const RadialState &state,
		QPoint position,
		QSize size,
		int outerWidth,
		QPen pen,
		int thickness);

	[[nodiscard]] RadialState computeState();

private:
	const style::InfiniteRadialAnimation &_st;
	crl::time _workStarted = 0;
	crl::time _workFinished = 0;
	Ui::Animations::Basic _animation;

};

template <typename Callback>
inline InfiniteRadialAnimation::InfiniteRadialAnimation(
	Callback &&callback,
	const style::InfiniteRadialAnimation &st)
: _st(st)
, _animation(std::forward<Callback>(callback)) {
}

} // namespace Ui
