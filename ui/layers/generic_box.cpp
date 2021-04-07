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

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_owned));
	setDimensionsToContent(_width ? _width : st::boxWidth, wrap.data());
	setInnerWidget(std::move(wrap));
}

void GenericBox::addSkip(int height) {
	addRow(object_ptr<Ui::FixedHeightWidget>(this, height));
}

not_null<Ui::VerticalLayout*> GenericBox::verticalLayout() {
	return _content;
}

} // namespace Ui
