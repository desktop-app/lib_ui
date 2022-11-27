// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animations.h"
#include "ui/wrap/wrap.h"

namespace Ui {

template <typename Widget = RpWidget>
class FollowSlideWrap;

template <>
class FollowSlideWrap<RpWidget> : public Wrap<RpWidget> {
	using Parent = Wrap<RpWidget>;

public:
	FollowSlideWrap(
		QWidget *parent,
		object_ptr<RpWidget> &&child);

	FollowSlideWrap *setDuration(crl::time duration);
	int naturalWidth() const override;

protected:
	void wrappedSizeUpdated(QSize size) override;

private:
	void updateWrappedPosition(int forHeight);

	Animations::Simple _animation;
	crl::time _duration = 0;

};

template <typename Widget>
class FollowSlideWrap : public Wrap<Widget, FollowSlideWrap<RpWidget>> {
	using Parent = Wrap<Widget, FollowSlideWrap<RpWidget>>;

public:
	FollowSlideWrap(
		QWidget *parent,
		object_ptr<Widget> &&child)
	: Parent(parent, std::move(child)) {
	}

};

} // namespace Ui

