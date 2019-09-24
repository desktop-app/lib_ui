// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animations.h"

namespace Ui {

class SlideAnimation {
public:
	struct State {
		float64 leftProgress = 0.;
		float64 leftAlpha = 0.;
		float64 rightProgress = 0.;
		float64 rightAlpha = 0.;
	};

	void setSnapshots(QPixmap leftSnapshot, QPixmap rightSnapshot);

	void setOverflowHidden(bool hidden) {
		_overflowHidden = hidden;
	}

	template <typename Lambda>
	void start(bool slideLeft, Lambda &&updateCallback, float64 duration);

	void paintFrame(QPainter &p, int x, int y, int outerWidth);

	[[nodiscard]] State state() const;
	[[nodiscard]] bool animating() const {
		return _animation.animating();
	}

private:
	Ui::Animations::Simple _animation;
	QPixmap _leftSnapshot;
	QPixmap _rightSnapshot;
	bool _slideLeft = false;
	bool _overflowHidden = true;
	int _leftSnapshotWidth = 0;
	int _leftSnapshotHeight = 0;
	int _rightSnapshotWidth = 0;

};

template <typename Lambda>
void SlideAnimation::start(
		bool slideLeft,
		Lambda &&updateCallback,
		float64 duration) {
	_slideLeft = slideLeft;
	if (_slideLeft) {
		std::swap(_leftSnapshot, _rightSnapshot);
	}
	const auto pixelRatio = style::DevicePixelRatio();
	_leftSnapshotWidth = _leftSnapshot.width() / pixelRatio;
	_leftSnapshotHeight = _leftSnapshot.height() / pixelRatio;
	_rightSnapshotWidth = _rightSnapshot.width() / pixelRatio;
	_animation.start(std::forward<Lambda>(updateCallback), 0., 1., duration);
}

} // namespace Ui
