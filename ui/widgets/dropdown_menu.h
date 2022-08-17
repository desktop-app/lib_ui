// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "styles/style_widgets.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/menu/menu.h"

namespace Ui {

class DropdownMenu : public InnerDropdown {
public:
	DropdownMenu(QWidget *parent, const style::DropdownMenu &st = st::defaultDropdownMenu);

	not_null<QAction*> addAction(base::unique_qptr<Menu::ItemBase> widget);
	not_null<QAction*> addAction(const QString &text, Fn<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	not_null<QAction*> addSeparator(
		const style::MenuSeparator *st = nullptr);
	void clearActions();

	void setHiddenCallback(Fn<void()> callback) {
		_hiddenCallback = std::move(callback);
	}

	[[nodiscard]] const std::vector<not_null<QAction*>> &actions() const;
	[[nodiscard]] bool empty() const;

	[[nodiscard]] not_null<Menu::Menu*> menu() const {
		return _menu;
	}

	~DropdownMenu();

protected:
	void focusOutEvent(QFocusEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	// Not ready with submenus yet.
	DropdownMenu(QWidget *parent, QMenu *menu, const style::DropdownMenu &st = st::defaultDropdownMenu);
	void deleteOnHide(bool del);
	void popup(const QPoint &p);
	void hideMenu(bool fast = false);

	void childHiding(DropdownMenu *child);

	void init();
	void hideFinish();

	using TriggeredSource = Menu::TriggeredSource;
	void handleActivated(const Menu::CallbackData &data);
	void handleTriggered(const Menu::CallbackData &data);
	void forwardKeyPress(not_null<QKeyEvent*> e);
	bool handleKeyPress(int key);
	void forwardMouseMove(QPoint globalPosition) {
		_menu->handleMouseMove(globalPosition);
	}
	void handleMouseMove(QPoint globalPosition);
	void forwardMousePress(QPoint globalPosition) {
		_menu->handleMousePress(globalPosition);
	}
	void handleMousePress(QPoint globalPosition);
	void forwardMouseRelease(QPoint globalPosition) {
		_menu->handleMouseRelease(globalPosition);
	}
	void handleMouseRelease(QPoint globalPosition);

	using SubmenuPointer = QPointer<DropdownMenu>;
	bool popupSubmenuFromAction(const Menu::CallbackData &data);
	void popupSubmenu(SubmenuPointer submenu, int actionTop, TriggeredSource source);
	void showMenu(const QPoint &p, DropdownMenu *parent, TriggeredSource source);

	const style::DropdownMenu &_st;
	Fn<void()> _hiddenCallback;

	QPointer<Menu::Menu> _menu;

	// Not ready with submenus yet.
	//using Submenus = QMap<QAction*, SubmenuPointer>;
	//Submenus _submenus;

	DropdownMenu *_parent = nullptr;

	SubmenuPointer _activeSubmenu;

	bool _deleteOnHide = false;
	bool _triggering = false;
	bool _deleteLater = false;

};

} // namespace Ui
