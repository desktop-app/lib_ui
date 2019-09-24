// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/slide_animation.h"

#include <QtGui/QPainter>

namespace Ui {

void SlideAnimation::setSnapshots(
		QPixmap leftSnapshot,
		QPixmap rightSnapshot) {
	Expects(!leftSnapshot.isNull());
	Expects(!rightSnapshot.isNull());

	_leftSnapshot = std::move(leftSnapshot);
	_rightSnapshot = std::move(rightSnapshot);
	_leftSnapshot.setDevicePixelRatio(style::DevicePixelRatio());
	_rightSnapshot.setDevicePixelRatio(style::DevicePixelRatio());
}

void SlideAnimation::paintFrame(QPainter &p, int x, int y, int outerWidth) {
	if (!animating()) {
		return;
	}

	const auto pixelRatio = style::DevicePixelRatio();
	const auto state = this->state();
	const auto leftCoord = _slideLeft
		? anim::interpolate(-_leftSnapshotWidth, 0, state.leftProgress)
		: anim::interpolate(0, -_leftSnapshotWidth, state.leftProgress);
	const auto rightCoord = _slideLeft
		? anim::interpolate(0, _rightSnapshotWidth, state.rightProgress)
		: anim::interpolate(_rightSnapshotWidth, 0, state.rightProgress);

	if (_overflowHidden) {
		const auto leftWidth = (_leftSnapshotWidth + leftCoord);
		if (leftWidth > 0) {
			p.setOpacity(state.leftAlpha);
			p.drawPixmap(
				x,
				y,
				leftWidth,
				_leftSnapshotHeight,
				_leftSnapshot,
				(_leftSnapshot.width() - leftWidth * pixelRatio),
				0,
				leftWidth * pixelRatio,
				_leftSnapshot.height());
		}
		const auto rightWidth = _rightSnapshotWidth - rightCoord;
		if (rightWidth > 0) {
			p.setOpacity(state.rightAlpha);
			p.drawPixmap(
				x + rightCoord,
				y,
				_rightSnapshot,
				0,
				0,
				rightWidth * pixelRatio,
				_rightSnapshot.height());
		}
	} else {
		p.setOpacity(state.leftAlpha);
		p.drawPixmap(x + leftCoord, y, _leftSnapshot);
		p.setOpacity(state.rightAlpha);
		p.drawPixmap(x + rightCoord, y, _rightSnapshot);
	}
}

SlideAnimation::State SlideAnimation::state() const {
	const auto dt = _animation.value(1.);
	const auto easeOut = anim::easeOutCirc(1., dt);
	const auto easeIn = anim::easeInCirc(1., dt);
	const auto arrivingAlpha = easeIn;
	const auto departingAlpha = 1. - easeOut;

	auto result = State();
	result.leftProgress = _slideLeft ? easeOut : easeIn;
	result.leftAlpha = _slideLeft ? arrivingAlpha : departingAlpha;
	result.rightProgress = _slideLeft ? easeIn : easeOut;
	result.rightAlpha = _slideLeft ? departingAlpha : arrivingAlpha;
	return result;
}

} // namespace Ui
