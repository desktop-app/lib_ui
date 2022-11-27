// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/wrap/follow_slide_wrap.h"

#include "styles/style_basic.h"

namespace Ui {

FollowSlideWrap<RpWidget>::FollowSlideWrap(
	QWidget *parent,
	object_ptr<RpWidget> &&child)
: Parent(parent, std::move(child))
, _duration(st::slideWrapDuration) {
	if (const auto weak = wrapped()) {
		wrappedSizeUpdated(weak->size());
	}
}

FollowSlideWrap<RpWidget> *FollowSlideWrap<RpWidget>::setDuration(
		crl::time duration) {
	_duration = duration;
	return this;
}

int FollowSlideWrap<RpWidget>::naturalWidth() const {
	return -1;
}

void FollowSlideWrap<RpWidget>::wrappedSizeUpdated(QSize size) {
	updateWrappedPosition(size.height());
}

void FollowSlideWrap<RpWidget>::updateWrappedPosition(int forHeight) {
	_animation.stop();
	_animation.start(
		[=](float64 value) { resize(width(), value); },
		height(),
		forHeight,
		_duration);
}

} // namespace Ui

