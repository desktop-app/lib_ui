// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_common.h"
#include "styles/style_widgets.h"

namespace Ui::Menu {

class ItemBase : public RippleButton {
public:
	ItemBase(not_null<RpWidget*> parent, const style::Menu &st);

	TriggeredSource lastTriggeredSource() const;

	rpl::producer<CallbackData> selects() const;
	void setSelected(
		bool selected,
		TriggeredSource source = TriggeredSource::Mouse);
	bool isSelected() const;

	int index() const;
	void setIndex(int index);

	void setClicked(TriggeredSource source = TriggeredSource::Mouse);

	rpl::producer<CallbackData> clicks() const;

	rpl::producer<int> minWidthValue() const;
	int minWidth() const;
	void setMinWidth(int w);

	virtual void handleKeyPress(not_null<QKeyEvent*> e) {
	}

	virtual not_null<QAction*> action() const = 0;
	virtual bool isEnabled() const = 0;

	virtual void finishAnimating();

protected:
	void initResizeHook(rpl::producer<QSize> &&size);

	void enableMouseSelecting();
	void enableMouseSelecting(not_null<RpWidget*> widget);

	virtual int contentHeight() const = 0;

private:
	int _index = -1;

	rpl::variable<bool> _selected = false;
	rpl::event_stream<> _clicks;

	rpl::variable<int> _minWidth = 0;

	TriggeredSource _lastTriggeredSource = TriggeredSource::Mouse;

};

} // namespace Ui::Menu
