// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/wrap/padding_wrap.h"

#include "ui/ui_utility.h"

namespace Ui {

PaddingWrap<RpWidget>::PaddingWrap(
	QWidget *parent,
	object_ptr<RpWidget> &&child,
	const style::margins &padding)
: Parent(parent, std::move(child)) {
	setPadding(padding);
	if (auto weak = wrapped()) {
		wrappedNaturalWidthUpdated(weak->naturalWidth());
	}
}

void PaddingWrap<RpWidget>::setPadding(const style::margins &padding) {
	if (_padding != padding) {
		auto oldWidth = width() - _padding.left() - _padding.top();
		_padding = padding;

		if (auto weak = wrapped()) {
			wrappedSizeUpdated(weak->size());

			auto margins = weak->getMargins();
			weak->moveToLeft(
				_padding.left() + margins.left(),
				_padding.top() + margins.top());
		} else {
			resize(QSize(
				_padding.left() + oldWidth + _padding.right(),
				_padding.top() + _padding.bottom()));
		}
	}
}

void PaddingWrap<RpWidget>::wrappedSizeUpdated(QSize size) {
	resize(QRect(QPoint(), size).marginsAdded(_padding).size());
}

void PaddingWrap<RpWidget>::wrappedNaturalWidthUpdated(int width) {
	setNaturalWidth((width < 0)
		? width
		: (_padding.left() + width + _padding.right()));
}

int PaddingWrap<RpWidget>::resizeGetHeight(int newWidth) {
	if (auto weak = wrapped()) {
		weak->resizeToWidth(newWidth
			- _padding.left()
			- _padding.right());
		SendPendingMoveResizeEvents(weak);
	} else {
		resize(QSize(
			_padding.left() + newWidth + _padding.right(),
			_padding.top() + _padding.bottom()));
	}
	return heightNoMargins();
}

} // namespace Ui
