// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/layers/generic_box.h"

#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/wrap.h"
#include "styles/style_layers.h"

namespace Ui {

void GenericBox::prepare() {
	_init(this);

	const auto currentWidth = width();
	const auto pinnedToTop = _pinnedToTopContent.data();
	const auto pinnedToBottom = _pinnedToBottomContent.data();
	if (pinnedToTop) {
		pinnedToTop->resizeToWidth(currentWidth);
	}
	if (pinnedToBottom) {
		pinnedToBottom->resizeToWidth(currentWidth);
	}

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_owned));
	wrap->resizeToWidth(currentWidth);
	rpl::combine(
		pinnedToTop ? pinnedToTop->heightValue() : rpl::single(0),
		wrap->heightValue(),
		pinnedToBottom ? pinnedToBottom->heightValue() : rpl::single(0)
	) | rpl::start_with_next([=](int top, int height, int bottom) {
		Expects(_minHeight >= 0);
		Expects(!_maxHeight || _minHeight <= _maxHeight);

		setInnerTopSkip(top);
		setInnerBottomSkip(bottom);
		const auto desired = top + height + bottom;
		setDimensions(
			currentWidth,
			std::clamp(
				desired,
				_minHeight,
				_maxHeight ? _maxHeight : std::max(_minHeight, desired)),
			true);
	}, wrap->lifetime());

	setInnerWidget(
		std::move(wrap),
		_scrollSt ? *_scrollSt : st::boxScroll,
		pinnedToTop ? pinnedToTop->height() : 0,
		pinnedToBottom ? pinnedToBottom->height() : 0);

	if (pinnedToBottom) {
		rpl::combine(
			heightValue(),
			pinnedToBottom->heightValue()
		) | rpl::start_with_next([=](int outer, int height) {
			pinnedToBottom->move(0, outer - height);
		}, pinnedToBottom->lifetime());
	}

	if (const auto onstack = _initScroll) {
		onstack();
	}
}

void GenericBox::addSkip(int height) {
	addRow(object_ptr<Ui::FixedHeightWidget>(this, height));
}

void GenericBox::setInnerFocus() {
	if (_focus) {
		_focus();
	} else {
		BoxContent::setInnerFocus();
	}
}

void GenericBox::showFinished() {
	const auto guard = QPointer(this);
	if (const auto onstack = _showFinished) {
		onstack();
		if (!guard) {
			return;
		}
	}
	_showFinishes.fire({});
}

not_null<Ui::RpWidget*> GenericBox::doSetPinnedToTopContent(
		object_ptr<Ui::RpWidget> content) {
	_pinnedToTopContent = std::move(content);
	return _pinnedToTopContent.data();
}

not_null<Ui::RpWidget*> GenericBox::doSetPinnedToBottomContent(
		object_ptr<Ui::RpWidget> content) {
	_pinnedToBottomContent = std::move(content);
	return _pinnedToBottomContent.data();
}

int GenericBox::rowsCount() const {
	return _content->count();
}

int GenericBox::width() const {
	return _width ? _width : st::boxWidth;
}

not_null<Ui::VerticalLayout*> GenericBox::verticalLayout() {
	return _content;
}

rpl::producer<> BoxShowFinishes(not_null<GenericBox*> box) {
	const auto singleShot = box->lifetime().make_state<rpl::lifetime>();
	const auto showFinishes = singleShot->make_state<rpl::event_stream<>>();

	box->setShowFinishedCallback([=] {
		showFinishes->fire({});
		singleShot->destroy();
		box->setShowFinishedCallback(nullptr);
	});

	return showFinishes->events();
}

} // namespace Ui
