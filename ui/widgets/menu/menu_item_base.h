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
	ItemBase(not_null<RpWidget*> parent, const style::Menu &st, int index);

	TriggeredSource lastTriggeredSource() const;

	rpl::producer<CallbackData> selects() const;
	void setSelected(
		bool selected,
		TriggeredSource source = TriggeredSource::Mouse);
	bool isSelected() const;

	int index() const;

	void setClicked(TriggeredSource source = TriggeredSource::Mouse);

	rpl::producer<CallbackData> clicks() const;

	rpl::producer<int> contentWidthValue() const;
	int contentWidth() const;
	void setContentWidth(int w);

	bool hasSubmenu() const;
	void setHasSubmenu(bool value);

	virtual QAction *action() const = 0;
	virtual bool isEnabled() const = 0;

protected:
	void init();
	void initResizeHook(rpl::producer<QSize> &&size);

	virtual int contentHeight() const = 0;

private:
	const int _index;

	bool _hasSubmenu = false;

	rpl::variable<bool> _selected = false;
	rpl::event_stream<> _clicks;

	rpl::variable<int> _contentWidth = 0;

	TriggeredSource _lastTriggeredSource = TriggeredSource::Mouse;

};

} // namespace Ui::Menu
