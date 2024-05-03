// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/radial_animation.h"

#include "ui/arc_angles.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kFullArcLength = arc::kFullLength;

} // namespace

const int RadialState::kFull = kFullArcLength;

void RadialAnimation::start(float64 prg) {
	_firstStart = _lastStart = _lastTime = crl::now();
	const auto iprg = qRound(qMax(prg, 0.0001) * arc::kAlmostFullLength);
	const auto iprgstrict = qRound(prg * arc::kAlmostFullLength);
	_arcEnd = anim::value(iprgstrict, iprg);
	_animation.start();
}

bool RadialAnimation::update(float64 prg, bool finished, crl::time ms) {
	const auto iprg = qRound(qMax(prg, 0.0001) * arc::kAlmostFullLength);
	const auto result = (iprg != qRound(_arcEnd.to()))
		|| (_finished != finished);
	if (_finished != finished) {
		_arcEnd.start(iprg);
		_finished = finished;
		_lastStart = _lastTime;
	} else if (result) {
		_arcEnd.start(iprg);
		_lastStart = _lastTime;
	}
	_lastTime = ms;

	const auto dt = float64(ms - _lastStart);
	const auto fulldt = float64(ms - _firstStart);
	const auto opacitydt = _finished
		? (_lastStart - _firstStart)
		: fulldt;
	_opacity = qMin(opacitydt / st::radialDuration, 1.);
	if (anim::Disabled()) {
		_arcEnd.update(1., anim::linear);
		if (finished) {
			stop();
		}
	} else if (!finished) {
		_arcEnd.update(1. - (st::radialDuration / (st::radialDuration + dt)), anim::linear);
	} else if (dt >= st::radialDuration) {
		_arcEnd.update(1., anim::linear);
		stop();
	} else {
		auto r = dt / st::radialDuration;
		_arcEnd.update(r, anim::linear);
		_opacity *= 1 - r;
	}
	auto fromstart = fulldt / st::radialPeriod;
	_arcStart.update(fromstart - std::floor(fromstart), anim::linear);
	return result;
}

void RadialAnimation::stop() {
	_firstStart = _lastStart = _lastTime = 0;
	_arcEnd = anim::value();
	_animation.stop();
}

void RadialAnimation::draw(
		QPainter &p,
		const QRect &inner,
		int32 thickness,
		style::color color) const {
	const auto state = computeState();

	auto o = p.opacity();
	p.setOpacity(o * state.shown);

	auto pen = color->p;
	auto was = p.pen();
	pen.setWidth(thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	{
		PainterHighQualityEnabler hq(p);
		p.drawArc(inner, state.arcFrom, state.arcLength);
	}

	p.setPen(was);
	p.setOpacity(o);
}

RadialState RadialAnimation::computeState() const {
	auto length = arc::kMinLength + qRound(_arcEnd.current());
	auto from = arc::kQuarterLength
		- length
		- (anim::Disabled() ? 0 : qRound(_arcStart.current()));
	if (style::RightToLeft()) {
		from = arc::kQuarterLength - (from - arc::kQuarterLength) - length;
		if (from < 0) from += arc::kFullLength;
	}
	return { _opacity, from, length };
}

void InfiniteRadialAnimation::init() {
	anim::Disables() | rpl::filter([=] {
		return animating();
	}) | rpl::start_with_next([=](bool disabled) {
		if (!disabled && !_animation.animating()) {
			_animation.start();
		} else if (disabled && _animation.animating()) {
			_animation.stop();
		}
	}, _lifetime);
}

void InfiniteRadialAnimation::start(crl::time skip) {
	if (!animating()) {
		const auto now = crl::now();
		_workStarted = std::max(now + _st.sineDuration - skip, crl::time(1));
		_workFinished = 0;
	}
	if (!anim::Disabled() && !_animation.animating()) {
		_animation.start();
	}
}

void InfiniteRadialAnimation::stop(anim::type animated) {
	const auto now = crl::now();
	if (anim::Disabled() || animated == anim::type::instant) {
		_workFinished = now;
	}
	if (!_workFinished) {
		const auto zero = _workStarted - _st.sineDuration;
		const auto index = (now - zero + _st.sinePeriod - _st.sineShift)
			/ _st.sinePeriod;
		_workFinished = zero
			+ _st.sineShift
			+ (index * _st.sinePeriod)
			+ _st.sineDuration;
	} else if (_workFinished <= now) {
		_animation.stop();
	}
}

void InfiniteRadialAnimation::draw(
		QPainter &p,
		QPoint position,
		int outerWidth) {
	Draw(
		p,
		computeState(),
		position,
		_st.size,
		outerWidth,
		_st.color,
		_st.thickness);
}

void InfiniteRadialAnimation::draw(
		QPainter &p,
		QPoint position,
		QSize size,
		int outerWidth) {
	Draw(
		p,
		computeState(),
		position,
		size,
		outerWidth,
		_st.color,
		_st.thickness);
}

void InfiniteRadialAnimation::Draw(
		QPainter &p,
		const RadialState &state,
		QPoint position,
		QSize size,
		int outerWidth,
		QPen pen,
		int thickness) {
	auto o = p.opacity();
	p.setOpacity(o * state.shown);

	const auto rect = style::rtlrect(
		position.x(),
		position.y(),
		size.width(),
		size.height(),
		outerWidth);
	const auto was = p.pen();
	const auto brush = p.brush();
	if (anim::Disabled()) {
		anim::DrawStaticLoading(p, rect, thickness, pen);
	} else {
		pen.setWidth(thickness);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);

		{
			PainterHighQualityEnabler hq(p);
			p.drawArc(
				rect,
				state.arcFrom,
				state.arcLength);
		}
	}
	p.setPen(was);
	p.setBrush(brush);
	p.setOpacity(o);
}

RadialState InfiniteRadialAnimation::computeState() {
	const auto now = crl::now();
	const auto linear = kFullArcLength
		- int(((now * kFullArcLength) / _st.linearPeriod) % kFullArcLength);
	if (!animating()) {
		const auto shown = 0.;
		_animation.stop();
		return {
			shown,
			linear,
			kFullArcLength };
	}
	if (anim::Disabled()) {
		return { 1., 0, kFullArcLength };
	}
	const auto min = int(base::SafeRound(kFullArcLength * _st.arcMin));
	const auto max = int(base::SafeRound(kFullArcLength * _st.arcMax));
	if (now <= _workStarted) {
		// zero .. _workStarted
		const auto zero = _workStarted - _st.sineDuration;
		const auto shown = (now - zero) / float64(_st.sineDuration);
		const auto length = anim::interpolate(
			kFullArcLength,
			min,
			anim::sineInOut(1., std::clamp(shown, 0., 1.)));
		return {
			shown,
			linear,
			length };
	} else if (!_workFinished || now <= _workFinished - _st.sineDuration) {
		// _workStared .. _workFinished - _st.sineDuration
		const auto shown = 1.;
		const auto cycles = (now - _workStarted) / _st.sinePeriod;
		const auto relative = (now - _workStarted) % _st.sinePeriod;
		const auto smallDuration = _st.sineShift - _st.sineDuration;
		const auto basic = int((linear
			+ min
			+ (cycles * (kFullArcLength + min - max))) % kFullArcLength);
		if (relative <= smallDuration) {
			// localZero .. growStart
			return {
				shown,
				basic - min,
				min };
		} else if (relative <= smallDuration + _st.sineDuration) {
			// growStart .. growEnd
			const auto growLinear = (relative - smallDuration) /
				float64(_st.sineDuration);
			const auto growProgress = anim::sineInOut(1., growLinear);
			const auto length = anim::interpolate(min, max, growProgress);
			return {
				shown,
				basic - length,
				length };
		} else if (relative <= _st.sinePeriod - _st.sineDuration) {
			// growEnd .. shrinkStart
			return {
				shown,
				basic - max,
				max };
		} else {
			// shrinkStart .. shrinkEnd
			const auto shrinkLinear = (relative
				- (_st.sinePeriod - _st.sineDuration))
					/ float64(_st.sineDuration);
			const auto shrinkProgress = anim::sineInOut(1., shrinkLinear);
			const auto shrink = anim::interpolate(
				0,
				max - min,
				shrinkProgress);
			return {
				shown,
				basic - max,
				max - shrink }; // interpolate(max, min, shrinkProgress)
		}
	} else {
		// _workFinished - _st.sineDuration .. _workFinished
		const auto hidden = (now - (_workFinished - _st.sineDuration))
			/ float64(_st.sineDuration);
		const auto cycles = (_workFinished - _workStarted) / _st.sinePeriod;
		const auto basic = int((linear
			+ min
			+ cycles * (kFullArcLength + min - max)) % kFullArcLength);
		const auto length = anim::interpolate(
			min,
			kFullArcLength,
			anim::sineInOut(1., std::clamp(hidden, 0., 1.)));
		return {
			1. - hidden,
			basic - length,
			length };
	}
	//const auto frontPeriods = time / st.sinePeriod;
	//const auto frontCurrent = time % st.sinePeriod;
	//const auto frontProgress = anim::sineInOut(
	//	st.arcMax - st.arcMin,
	//	std::min(frontCurrent, crl::time(st.sineDuration))
	//	/ float64(st.sineDuration));
	//const auto backTime = std::max(time - st.sineShift, 0LL);
	//const auto backPeriods = backTime / st.sinePeriod;
	//const auto backCurrent = backTime % st.sinePeriod;
	//const auto backProgress = anim::sineInOut(
	//	st.arcMax - st.arcMin,
	//	std::min(backCurrent, crl::time(st.sineDuration))
	//	/ float64(st.sineDuration));
	//const auto front = linear + base::SafeRound((st.arcMin + frontProgress + frontPeriods * (st.arcMax - st.arcMin)) * kFullArcLength);
	//const auto from = linear + base::SafeRound((backProgress + backPeriods * (st.arcMax - st.arcMin)) * kFullArcLength);
	//const auto length = (front - from);

	//return {
	//	_opacity,
	//	from,
	//	length
	//};
}

} // namespace Ui
