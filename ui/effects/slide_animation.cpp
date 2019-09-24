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
	const auto dt = _animation.value(1.);
	if (!animating()) {
		return;
	}

	const auto pixelRatio = style::DevicePixelRatio();
	const auto easeOut = anim::easeOutCirc(1., dt);
	const auto easeIn = anim::easeInCirc(1., dt);
	const auto arrivingAlpha = easeIn;
	const auto departingAlpha = 1. - easeOut;
	const auto leftCoord = _slideLeft
		? anim::interpolate(-_leftSnapshotWidth, 0, easeOut)
		: anim::interpolate(0, -_leftSnapshotWidth, easeIn);
	const auto leftAlpha = (_slideLeft ? arrivingAlpha : departingAlpha);
	const auto rightCoord = _slideLeft
		? anim::interpolate(0, _rightSnapshotWidth, easeIn)
		: anim::interpolate(_rightSnapshotWidth, 0, easeOut);
	const auto rightAlpha = (_slideLeft ? departingAlpha : arrivingAlpha);

	if (_overflowHidden) {
		const auto leftWidth = (_leftSnapshotWidth + leftCoord);
		if (leftWidth > 0) {
			p.setOpacity(leftAlpha);
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
			p.setOpacity(rightAlpha);
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
		p.setOpacity(leftAlpha);
		p.drawPixmap(x + leftCoord, y, _leftSnapshot);
		p.setOpacity(rightAlpha);
		p.drawPixmap(x + rightCoord, y, _rightSnapshot);
	}
}

} // namespace Ui
