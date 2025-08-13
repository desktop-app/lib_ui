// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Ui {

class VerticalLayout : public RpWidget {
public:
	using RpWidget::RpWidget;

	[[nodiscard]] int count() const {
		return _rows.size();
	}
	[[nodiscard]] not_null<RpWidget*> widgetAt(int index) const {
		Expects(index >= 0 && index < count());

		return _rows[index].widget.data();
	}

	template <
		typename Widget,
		typename = std::enable_if_t<
			std::is_base_of_v<RpWidget, Widget>>>
	Widget *insert(
			int atPosition,
			object_ptr<Widget> &&child,
			const style::margins &margin = style::margins(),
			style::align align = style::al_left) {
		return static_cast<Widget*>(insertChild(
			atPosition,
			std::move(child),
			margin,
			align));
	}

	template <
		typename Widget,
		typename = std::enable_if_t<
			std::is_base_of_v<RpWidget, Widget>>>
	Widget *add(
			object_ptr<Widget> &&child,
			const style::margins &margin = style::margins(),
			style::align align = style::al_left) {
		return insert(count(), std::move(child), margin, align);
	}

	QMargins getMargins() const override;

	void setVerticalShift(int index, int shift);
	void reorderRows(int oldIndex, int newIndex);

	void clear();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	static constexpr auto kAlignLeft = 0;
	static constexpr auto kAlignCenter = 1;
	static constexpr auto kAlignRight = -1;
	static constexpr auto kAlignJustify = -2;

	RpWidget *insertChild(
		int addPosition,
		object_ptr<RpWidget> child,
		const style::margins &margin,
		style::align align);
	void subscribeToWidth(
		not_null<RpWidget*> child,
		const style::margins &margin);
	void childWidthUpdated(RpWidget *child);
	void childHeightUpdated(RpWidget *child);
	void removeChild(RpWidget *child);
	void updateChildGeometry(
		const style::margins &margins,
		RpWidget *child,
		const style::margins &margin,
		int align,
		int width,
		int top) const;
	void moveChild(
		const style::margins &margins,
		RpWidget *child,
		const style::margins &margin,
		int align,
		int width,
		int top) const;

	struct Row {
		object_ptr<RpWidget> widget;
		style::margins margin;
		int32 verticalShift : 30 = 0;
		int32 align : 2 = 0;
	};
	std::vector<Row> _rows;
	bool _inResize = false;

	rpl::lifetime _rowsLifetime;

};

} // namespace Ui
