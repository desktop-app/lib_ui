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
	if (_pinnedToTopContent) {
		_pinnedToTopContent->resizeToWidth(currentWidth);
	}

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_owned));
	setDimensionsToContent(currentWidth, wrap.data());
	setInnerWidget(
		std::move(wrap),
		_pinnedToTopContent ? _pinnedToTopContent->height() : 0);
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

} // namespace Ui
