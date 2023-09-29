// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/object_ptr.h"
#include "ui/rp_widget.h"

namespace style {
struct Table;
} // namespace style

namespace st {
extern const style::Table &defaultTable;
} // namespace st

namespace Ui {

class TableLayout : public RpWidget {
public:
	TableLayout(QWidget *parent, const style::Table &st = st::defaultTable);

	[[nodiscard]] int rowsCount() const {
		return _rows.size();
	}

	[[nodiscard]] not_null<RpWidget*> labelAt(int index) const {
		Expects(index >= 0 && index < rowsCount());

		return _rows[index].label.data();
	}
	[[nodiscard]] not_null<RpWidget*> valueAt(int index) const {
		Expects(index >= 0 && index < rowsCount());

		return _rows[index].value.data();
	}

	void insertRow(
		int atPosition,
		object_ptr<RpWidget> &&label,
		object_ptr<RpWidget> &&value,
		const style::margins &labelMargin = style::margins(),
		const style::margins &valueMargin = style::margins());

	void addRow(
			object_ptr<RpWidget> &&label,
			object_ptr<RpWidget> &&value,
			const style::margins &labelMargin = style::margins(),
			const style::margins &valueMargin = style::margins()) {
		insertRow(
			rowsCount(),
			std::move(label),
			std::move(value),
			labelMargin,
			valueMargin);
	}

	void clear();

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	struct Row {
		object_ptr<RpWidget> label;
		object_ptr<RpWidget> value;
		style::margins labelMargin;
		style::margins valueMargin;
		mutable int top = 0;
	};

	[[nodiscard]] int rowVerticalSkip(const Row &row) const;
	void childHeightUpdated(RpWidget *child);
	void removeChild(RpWidget *child);
	void updateRowGeometry(const Row &row, int width, int top) const;

	const style::Table &_st;

	std::vector<Row> _rows;
	int _valueLeft = 0;
	bool _inResize = false;

	rpl::lifetime _rowsLifetime;

};

} // namespace Ui
