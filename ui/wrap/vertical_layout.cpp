// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/wrap/vertical_layout.h"

#include "ui/ui_utility.h"

namespace Ui {

QMargins VerticalLayout::getMargins() const {
	auto result = QMargins();
	if (!_rows.empty()) {
		auto &top = _rows.front();
		auto topMargin = top.widget->getMargins().top();
		result.setTop(
			qMax(topMargin - top.margin.top(), 0));
		auto &bottom = _rows.back();
		auto bottomMargin = bottom.widget->getMargins().bottom();
		result.setBottom(
			qMax(bottomMargin - bottom.margin.bottom(), 0));
		for (auto &row : _rows) {
			auto margins = row.widget->getMargins();
			result.setLeft(qMax(
				margins.left() - row.margin.left(),
				result.left()));
			result.setRight(qMax(
				margins.right() - row.margin.right(),
				result.right()));
		}
	}
	return result;
}

void VerticalLayout::setVerticalShift(int index, int shift) {
	Expects(index >= 0 && index < _rows.size());

	auto &row = _rows[index];
	if (const auto delta = shift - row.verticalShift) {
		row.verticalShift = shift;
		row.widget->move(row.widget->x(), row.widget->y() + delta);
		row.widget->update();
	}
}

void VerticalLayout::reorderRows(int oldIndex, int newIndex) {
	Expects(oldIndex >= 0 && oldIndex < _rows.size());
	Expects(newIndex >= 0 && newIndex < _rows.size());
	Expects(!_inResize);

	base::reorder(_rows, oldIndex, newIndex);
	resizeToWidth(width());
}

int VerticalLayout::resizeGetHeight(int newWidth) {
	if (newWidth <= 0) {
		return 0;
	}
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	const auto margins = getMargins();
	const auto outerWidth = margins.left() + newWidth + margins.right();
	auto result = margins.top();
	for (auto &row : _rows) {
		const auto widget = row.widget.data();
		const auto &margin = row.margin;
		const auto available = newWidth - margin.left() - margin.right();
		if (available > 0) {
			if (row.align == kAlignJustify) {
				widget->resizeToWidth(available);
			} else {
				widget->resizeToNaturalWidth(available);
			}
		}
		result += moveChildGetSkip(row, result, outerWidth, margins);
	}
	return result - margins.top();
}

void VerticalLayout::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	for (auto &row : _rows) {
		setChildVisibleTopBottom(
			row.widget,
			visibleTop,
			visibleBottom);
	}
}

int VerticalLayout::moveChildGetSkip(
		const Row &row,
		int top,
		int outerWidth,
		const style::margins &margins) const {
	const auto align = row.align;
	const auto widget = row.widget.data();
	const auto wmargins = widget->getMargins();
	const auto &margin = row.margin;
	const auto full = margins + margin;
	top += margin.top() + row.verticalShift;
	if (align == kAlignLeft || align == kAlignJustify) {
		widget->moveToLeft(full.left(), top, outerWidth);
	} else if (align == kAlignCenter) {
		const auto available = outerWidth - full.left() - full.right();
		const auto free = available
			- widget->width()
			- wmargins.left()
			- wmargins.right();
		widget->moveToLeft(full.left() + (free / 2), top, outerWidth);
	} else if (align == kAlignRight) {
		widget->moveToRight(full.right(), top, outerWidth);
	}
	return margin.top()
		- wmargins.top()
		+ widget->height()
		- wmargins.bottom()
		+ margin.bottom();
}

RpWidget *VerticalLayout::insertChild(
		int atPosition,
		object_ptr<RpWidget> child,
		const style::margins &margin,
		style::align align) {
	Expects(atPosition >= 0 && atPosition <= _rows.size());
	Expects(!_inResize);

	if (const auto weak = AttachParentChild(this, child)) {
		const auto converted = (align == style::al_justify)
			? kAlignJustify
			: (align & style::al_left)
			? kAlignLeft
			: (align & style::al_right)
			? kAlignRight
			: kAlignCenter;
		_rows.insert(
			begin(_rows) + atPosition,
			{ std::move(child), margin, 0, converted });

		if (converted != kAlignJustify) {
			subscribeToWidth(weak, margin);
		}

		weak->heightValue(
		) | rpl::start_with_next_done([=] {
			if (!_inResize) {
				childHeightUpdated(weak);
			}
		}, [=] {
			removeChild(weak);
		}, _rowsLifetime);

		return weak;
	}
	return nullptr;
}

void VerticalLayout::subscribeToWidth(
		not_null<RpWidget*> child,
		const style::margins &margin) {
	child->naturalWidthValue(
	) | rpl::start_with_next([=](int naturalWidth) {
		setNaturalWidth([&] {
			if (naturalWidth < 0) {
				return -1;
			}
			auto result = -1;
			for (const auto &row : _rows) {
				if (row.align == kAlignJustify) {
					return -1;
				}
				const auto natural = row.widget->naturalWidth();
				if (natural < 0) {
					return -1;
				}
				accumulate_max(
					result,
					row.margin.left() + natural + row.margin.right());
			}
			return result;
		}());

		const auto available = widthNoMargins()
			- margin.left()
			- margin.right();
		if (available > 0) {
			child->resizeToWidth((naturalWidth >= 0)
				? std::min(naturalWidth, available)
				: available);
		}
	}, _rowsLifetime);

	const auto taken = std::exchange(_inResize, true);
	child->widthValue(
	) | rpl::start_with_next([=] {
		if (!_inResize) {
			childWidthUpdated(child);
		}
	}, _rowsLifetime);
	_inResize = taken;
}

void VerticalLayout::childWidthUpdated(RpWidget *child) {
	const auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.widget == child);
	});
	const auto &row = *it;
	const auto margins = getMargins();
	const auto top = child->y()
		+ child->getMargins().top()
		- row.margin.top()
		- row.verticalShift;
	moveChildGetSkip(row, top, width(), margins);
}

void VerticalLayout::childHeightUpdated(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.widget == child);
	});

	const auto width = this->width();
	const auto margins = getMargins();
	auto top = [&] {
		if (it == _rows.begin()) {
			return margins.top();
		}
		auto prev = it - 1;
		const auto widget = prev->widget.data();
		return widget->y()
			+ widget->height()
			- widget->getMargins().bottom()
			+ prev->margin.bottom();
	}();
	for (auto end = _rows.end(); it != end; ++it) {
		const auto &row = *it;
		top += moveChildGetSkip(row, top, width, margins);
	}
	resize(width, top + margins.bottom());
}

void VerticalLayout::removeChild(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.widget == child);
	});
	auto end = _rows.end();
	Assert(it != end);

	const auto width = this->width();
	const auto margins = getMargins();
	auto top = [&] {
		if (it == _rows.begin()) {
			return margins.top();
		}
		auto prev = it - 1;
		const auto widget = prev->widget.data();
		return widget->y()
			+ widget->height()
			- widget->getMargins().bottom()
			+ prev->margin.bottom();
	}();
	for (auto next = it + 1; next != end; ++next) {
		const auto &row = *next;
		top += moveChildGetSkip(row, top, width, margins);
	}
	it->widget = nullptr;
	_rows.erase(it);

	resize(width, top + margins.bottom());
}

void VerticalLayout::clear() {
	while (!_rows.empty()) {
		removeChild(_rows.front().widget.data());
	}
}

} // namespace Ui
