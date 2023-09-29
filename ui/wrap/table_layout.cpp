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
	const auto inner = QRectF(rect()).marginsRemoved(
		{ half, half, half, half });

	p.setClipRect(0, 0, _valueLeft, height());
	p.setBrush(_st.headerBg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(inner, _st.radius, _st.radius);
	p.setClipping(false);

	auto path = QPainterPath();
	path.addRoundedRect(inner, _st.radius, _st.radius);
	auto top = half;
	for (auto i = 1, count = int(_rows.size()); i != count; ++i) {
		const auto y = _rows[i].top - half;
		path.moveTo(half, y);
		path.lineTo(width() - half, y);
	}
	path.moveTo(_valueLeft - half, half);
	path.lineTo(_valueLeft - half, height() - half);

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
		const auto natural = row.label->naturalWidth()
			+ row.labelMargin.left()
			+ row.labelMargin.right();
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
		setChildVisibleTopBottom(
			row.label,
			visibleTop,
			visibleBottom);
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
	row.top = top;
	row.label->resizeToNaturalWidth(_valueLeft
		- 2 * _st.border
		- row.labelMargin.left()
		- row.labelMargin.right());
	row.label->moveToLeft(
		_st.border + row.labelMargin.left(),
		top + row.labelMargin.top(),
		width);
	row.value->resizeToNaturalWidth(width
		- _valueLeft
		- _st.border
		- row.valueMargin.left()
		- row.valueMargin.right());
	row.value->moveToLeft(
		_valueLeft + row.valueMargin.left(),
		top + row.valueMargin.top(),
		width);
}

void TableLayout::insertRow(
		int atPosition,
		object_ptr<RpWidget> &&label,
		object_ptr<RpWidget> &&value,
		const style::margins &labelMargin,
		const style::margins &valueMargin) {
	Expects(atPosition >= 0 && atPosition <= _rows.size());
	Expects(!_inResize);

	const auto wlabel = AttachParentChild(this, label);
	const auto wvalue = AttachParentChild(this, value);
	if (wlabel && wvalue) {
		_rows.insert(begin(_rows) + atPosition, {
			std::move(label),
			std::move(value),
			labelMargin,
			valueMargin,
		});
		wlabel->heightValue(
		) | rpl::start_with_next_done([=] {
			if (!_inResize) {
				childHeightUpdated(wlabel);
			}
		}, [=] {
			removeChild(wlabel);
		}, _rowsLifetime);
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

	auto top = (it == _rows.begin()) ? _st.border : (it - 1)->top;
	const auto outer = width();
	for (auto end = _rows.end(); it != end; ++it) {
		const auto &row = *it;
		row.top = top;
		row.label->moveToLeft(
			_st.border + row.labelMargin.left(),
			top + row.labelMargin.top(),
			outer);
		row.value->moveToLeft(
			_valueLeft + row.valueMargin.left(),
			top + row.valueMargin.top(),
			outer);
		top += rowVerticalSkip(row);
	}
	resize(width(), _rows.empty() ? 0 : top);
}

void TableLayout::removeChild(RpWidget *child) {
	auto it = ranges::find_if(_rows, [child](const Row &row) {
		return (row.label == child) || (row.value == child);
	});
	auto end = _rows.end();
	Assert(it != end);

	auto top = (it == _rows.begin()) ? _st.border : (it - 1)->top;
	const auto outer = width();
	for (auto next = it + 1; next != end; ++next) {
		auto &row = *next;
		row.top = top;
		row.label->moveToLeft(
			_st.border + row.labelMargin.left(),
			top + row.labelMargin.top(),
			outer);
		row.value->moveToLeft(
			_valueLeft + row.valueMargin.left(),
			top + row.valueMargin.top(),
			outer);
		top += rowVerticalSkip(row);
	}
	it->label = nullptr;
	it->value = nullptr;
	_rows.erase(it);

	resize(width(), _rows.empty() ? 0 : top);
}

int TableLayout::rowVerticalSkip(const Row &row) const {
	const auto labelHeight = row.labelMargin.top()
		+ row.label->heightNoMargins()
		+ row.labelMargin.bottom();
	const auto valueHeight = row.valueMargin.top()
		+ row.value->heightNoMargins()
		+ row.valueMargin.bottom();
	return std::max(labelHeight, valueHeight) + _st.border;
}

void TableLayout::clear() {
	while (!_rows.empty()) {
		removeChild(_rows.front().label.data());
	}
}

} // namespace Ui
