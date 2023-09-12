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
	const auto pinned = _pinnedToTopContent.data();
	if (pinned) {
		pinned->resizeToWidth(currentWidth);
	}

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_owned));
	wrap->resizeToWidth(currentWidth);
	rpl::combine(
		pinned ? pinned->heightValue() : rpl::single(0),
		wrap->heightValue()
	) | rpl::start_with_next([=](int top, int height) {
		Expects(_minHeight >= 0);
		Expects(!_maxHeight || _minHeight <= _maxHeight);

		setInnerTopSkip(top);
		const auto desired = top + height;
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
		pinned ? pinned->height() : 0);
}

void GenericBox::addSkip(int height) {
	addRow(object_ptr<Ui::FixedHeightWidget>(this, height));
}

not_null<Ui::RpWidget*> GenericBox::doSetPinnedToTopContent(
		object_ptr<Ui::RpWidget> content) {
	_pinnedToTopContent = std::move(content);
	return _pinnedToTopContent.data();
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
