// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/menu/menu_item_base.h"

namespace style {
struct FlatLabel;
struct Menu;
} // namespace style

namespace Ui {
class FlatLabel;
class RpWidget;
} // namespace Ui

namespace Ui::Menu {

class MultilineAction final : public ItemBase {
public:
	MultilineAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		const style::FlatLabel &stLabel,
		QPoint labelPosition,
		TextWithEntities &&about,
		const style::icon *icon = nullptr,
		const style::icon *iconOver = nullptr);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;
	void paintEvent(QPaintEvent *e) override;
	void updateMinWidth();

	const style::Menu &_st;
	const style::icon *_icon;
	const style::icon *_iconOver;
	const QPoint _labelPosition;
	const base::unique_qptr<Ui::FlatLabel> _text;
	const not_null<QAction*> _dummyAction;

};

} // namespace Ui::Menu
