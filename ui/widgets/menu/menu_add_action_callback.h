// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core.h"

namespace anim {
enum class type : uchar;
} // namespace anim

namespace style {
struct MenuSeparator;
} // namespace style

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::Menu {

struct MenuCallback final {
public:
	struct Args {
		QString text;
		Fn<void()> handler;
		const style::icon *icon;
		const style::MenuSeparator *separatorSt = nullptr;
		Fn<void(not_null<Ui::PopupMenu*>)> fillSubmenu;
		Fn<bool()> triggerFilter;
		rpl::producer<anim::type> hideRequests;
		int addTopShift = 0;
		bool isSeparator = false;
		bool isAttention = false;
	};
	using Callback = Fn<QAction*(Args&&)>;

	explicit MenuCallback(Callback callback);

	QAction *operator()(Args &&args) const;
	QAction *operator()(
		const QString &text,
		Fn<void()> handler,
		const style::icon *icon) const;
private:
	Callback _callback;
};

} // namespace Ui::Menu
