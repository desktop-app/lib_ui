// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/wrap/table_layout.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

namespace Ui {

TableLayout::TableLayout(QWidget *parent, const style::Table &st)
: RpWidget(parent)
, _st(st) {
}

void TableLayout::paintEvent(QPaintEvent *e) {
	if (_rows.empty()) {
		return;
	}

	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto half = _st.border / 2.;

	auto yfrom = half;
	auto ytill = height() - half;
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		yfrom = _rows[i].top + half;
		if (_rows[i].label) {
			break;
		}
	}
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		const auto index = count - i - 1;
		if (_rows[index].label) {
			break;
		}
		ytill = _rows[index].top - half;
	}
	const auto inner = QRectF(rect()).marginsRemoved(
		{ half, half, half, half });

	if (ytill > yfrom) {
		p.setClipRect(0, yfrom, _valueLeft, ytill);
		p.setBrush(_st.headerBg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(inner, _st.radius, _st.radius);
		p.setClipping(false);
	}

	auto path = QPainterPath();
	path.addRoundedRect(inner, _st.radius, _st.radius);
	for (auto i = 1, count = int(_rows.size()); i != count; ++i) {
		const auto y = _rows[i].top - half;
		path.moveTo(half, y);
		path.lineTo(width() - half, y);
	}
	if (ytill > yfrom) {
		path.moveTo(_valueLeft - half, yfrom);
		path.lineTo(_valueLeft - half, ytill);
	}

	auto pen = _st.borderFg->p;
	pen.setWidth(_st.border);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

int TableLayout::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	auto available = newWidth - 3 * _st.border;
	const auto labelMax = int(base::SafeRound(
		_st.labelMaxWidth * available));
	const auto valueMin = available - labelMax;
	if (labelMax <= 0 || valueMin <= 0 || _rows.empty()) {
		return 0;
	}
	auto label = _st.labelMinWidth;
	for (auto &row : _rows) {
		const auto natural = row.label
			? (row.label->naturalWidth()
				+ row.labelMargin.left()
				+ row.labelMargin.right())
			: 0;
		if (natural < 0 || natural >= labelMax) {
			label = labelMax;
			break;
		} else if (natural > label) {
			label = natural;
		}
	}
	_valueLeft = _st.border * 2 + label;

	auto result = _st.border;
	for (auto &row : _rows) {
		updateRowGeometry(row, newWidth, result);
		result += rowVerticalSkip(row);
	}
	return result;
}

void TableLayout::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	for (auto &row : _rows) {
		if (row.label) {
			setChildVisibleTopBottom(
				row.label,
				visibleTop,
				visibleBottom);
		}
		setChildVisibleTopBottom(
			row.value,
			visibleTop,
			visibleBottom);
	}
}

void TableLayout::updateRowGeometry(
		const Row &row,
		int width,
		int top) const {
	if (row.label) {
		row.label->resizeToNaturalWidth(_valueLeft
			- 2 * _st.border
			- row.labelMargin.left()
			- row.labelMargin.right());
		row.value->resizeToNaturalWidth(width
			- _valueLeft
			- _st.border
			- row.valueMargin.left()
			- row.valueMargin.right());
	} else {
		row.value->resizeToNaturalWidth(width
			- 2 * _st.border
			- row.labelMargin.left()
			- row.valueMargin.right());
	}
	updateRowPosition(row, width, top);
}

void TableLayout::updateRowPosition(
		const Row &row,
		int width,
		int top) const {
	row.top = top;
	if (row.label) {
		row.label->moveToLeft(
			_st.border + row.labelMargin.left(),
			top + row.labelMargin.top(),
			width);
		row.value->moveToLeft(
			_valueLeft + row.valueMargin.left(),
			top + row.valueMargin.top(),
			width);
	} else {
		row.value->moveToLeft(
			_st.border + row.labelMargin.left(),
			top + row.valueMargin.top(),
			width);
	}
}

void TableLayout::insertRow(
		int atPosition,
		object_ptr<RpWidget> &&label,
		object_ptr<RpWidget> &&value,
		const style::margins &labelMargin,
		const style::margins &valueMargin) {
	Expects(atPosition >= 0 && atPosition <= _rows.size());
	Expects(!_inResize);

	const auto wlabel = label ? AttachParentChild(this, label) : nullptr;
	const auto wvalue = AttachParentChild(this, value);
	if (wvalue) {
		_rows.insert(begin(_rows) + atPosition, {
			std::move(label),
			std::move(value),
			labelMargin,
			valueMargin,
		});
		if (wlabel) {
			wlabel->heightValue(
			) | rpl::start_with_next_done([=] {
				if (!_inResize) {
					childHeightUpdated(wlabel);
				}
			}, [=] {
				removeChild(wlabel);
			}, _rowsLifetime);
		}
		wvalue->heightValue(
		) | rpl::start_with_next_done([=] {
			if (!_inResize) {
				childHeightUpdated(wvalue);
			}
		}, [=] {
			removeChild(wvalue);
		}, _rowsLifetime);
	}
}

void TableLayout::childHeightUpdated(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.label == child) || (row.value == child);
	});
	const auto end = _rows.end();
	Assert(it != end);

	auto top = it->top;
	const auto outer = width();
	for (; it != end; ++it) {
		const auto &row = *it;
		updateRowPosition(row, outer, top);
		top += rowVerticalSkip(row);
	}
	resize(width(), _rows.empty() ? 0 : top);
}

void TableLayout::removeChild(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.label == child) || (row.value == child);
	});
	const auto end = _rows.end();
	Assert(it != end);

	auto top = it->top;
	const auto outer = width();
	for (auto next = it + 1; next != end; ++next) {
		auto &row = *next;
		updateRowPosition(row, outer, top);
		top += rowVerticalSkip(row);
	}
	it->label = nullptr;
	it->value = nullptr;
	_rows.erase(it);

	resize(width(), _rows.empty() ? 0 : top);
}

int TableLayout::rowVerticalSkip(const Row &row) const {
	const auto labelHeight = row.label
		? (row.labelMargin.top()
			+ row.label->heightNoMargins()
			+ row.labelMargin.bottom())
		: 0;
	const auto valueHeight = row.valueMargin.top()
		+ row.value->heightNoMargins()
		+ row.valueMargin.bottom();
	return std::max(labelHeight, valueHeight) + _st.border;
}

void TableLayout::clear() {
	while (!_rows.empty()) {
		removeChild(_rows.front().value.data());
	}
}

} // namespace Ui
