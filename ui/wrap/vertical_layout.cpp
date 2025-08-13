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

	auto margins = getMargins();
	auto result = 0;
	for (auto &row : _rows) {
		updateChildGeometry(
			margins,
			row.widget,
			row.margin,
			row.align,
			newWidth,
			result + row.verticalShift);
		result += row.margin.top()
			+ row.widget->heightNoMargins()
			+ row.margin.bottom();
	}
	return result;
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

void VerticalLayout::updateChildGeometry(
		const style::margins &margins,
		RpWidget *child,
		const style::margins &margin,
		int align,
		int width,
		int top) const {
	const auto available = width - margin.left() - margin.right();
	if (available > 0) {
		if (align == kAlignJustify) {
			child->resizeToWidth(available);
		} else {
			child->resizeToNaturalWidth(available);
		}
	}
	moveChild(margins, child, margin, align, width, top);
}

void VerticalLayout::moveChild(
		const style::margins &margins,
		RpWidget *child,
		const style::margins &margin,
		int align,
		int width,
		int top) const {
	const auto outer = width + margins.left() + margins.right();
	if (align == kAlignLeft || align == kAlignJustify) {
		child->moveToLeft(
			margins.left() + margin.left(),
			margins.top() + margin.top() + top,
			outer);
	} else if (align == kAlignCenter) {
		const auto available = width - margin.left() - margin.right();
		const auto shift = (available - child->widthNoMargins()) / 2;
		child->moveToLeft(
			margins.left() + margin.left() + shift,
			margins.top() + margin.top() + top,
			outer);
	} else if (align == kAlignRight) {
		child->moveToRight(
			margins.right() + margin.right(),
			margins.top() + margin.top() + top,
			outer);
	}
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
	const auto margin = row.margin;
	const auto width = widthNoMargins();
	const auto margins = getMargins();
	const auto top = child->y() - margin.top() - margins.top();
	moveChild(margins, child, margin, row.align, width, top);
}

void VerticalLayout::childHeightUpdated(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.widget == child);
	});

	const auto width = widthNoMargins();
	const auto margins = getMargins();
	auto top = [&] {
		if (it == _rows.begin()) {
			return margins.top();
		}
		auto prev = it - 1;
		return prev->widget->bottomNoMargins() + prev->margin.bottom();
	}() - margins.top();
	for (auto end = _rows.end(); it != end; ++it) {
		const auto &row = *it;
		const auto margin = row.margin;
		const auto widget = row.widget.data();
		moveChild(margins, widget, margin, row.align, width, top);
		top += margin.top()
			+ widget->heightNoMargins()
			+ margin.bottom();
	}
	resize(this->width(), margins.top() + top + margins.bottom());
}

void VerticalLayout::removeChild(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.widget == child);
	});
	auto end = _rows.end();
	Assert(it != end);

	auto margins = getMargins();
	auto top = [&] {
		if (it == _rows.begin()) {
			return margins.top();
		}
		auto prev = it - 1;
		return prev->widget->bottomNoMargins() + prev->margin.bottom();
	}() - margins.top();
	for (auto next = it + 1; next != end; ++next) {
		auto &row = *next;
		auto margin = row.margin;
		auto widget = row.widget.data();
		widget->moveToLeft(
			margins.left() + margin.left(),
			margins.top() + top + margin.top());
		top += margin.top()
			+ widget->heightNoMargins()
			+ margin.bottom();
	}
	it->widget = nullptr;
	_rows.erase(it);

	resize(width(), margins.top() + top + margins.bottom());
}

void VerticalLayout::clear() {
	while (!_rows.empty()) {
		removeChild(_rows.front().widget.data());
	}
}

} // namespace Ui
