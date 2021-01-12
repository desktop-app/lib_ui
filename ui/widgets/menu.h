// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"
#include "ui/widgets/menu/menu_common.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QMenu>

namespace Ui {

class ItemBase;
class ToggleView;
class RippleAnimation;

class Menu : public RpWidget {
public:
	Menu(QWidget *parent, const style::Menu &st = st::defaultMenu);
	Menu(QWidget *parent, QMenu *menu, const style::Menu &st = st::defaultMenu);
	~Menu();

	not_null<QAction*> addAction(const QString &text, Fn<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	not_null<QAction*> addAction(const QString &text, std::unique_ptr<QMenu> submenu);
	not_null<QAction*> addSeparator();
	void clearActions();
	void finishAnimating();

	void clearSelection();

	using TriggeredSource = ContextMenu::TriggeredSource;
	void setChildShown(bool shown) {
		_childShown = shown;
	}
	void setShowSource(TriggeredSource source);
	void setForceWidth(int forceWidth);

	const std::vector<not_null<QAction*>> &actions() const;

	void setActivatedCallback(Fn<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_activatedCallback = std::move(callback);
	}
	void setTriggeredCallback(Fn<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_triggeredCallback = std::move(callback);
	}

	void setKeyPressDelegate(Fn<bool(int key)> delegate) {
		_keyPressDelegate = std::move(delegate);
	}
	void handleKeyPress(int key);

	void setMouseMoveDelegate(Fn<void(QPoint globalPosition)> delegate) {
		_mouseMoveDelegate = std::move(delegate);
	}
	void handleMouseMove(QPoint globalPosition);

	void setMousePressDelegate(Fn<void(QPoint globalPosition)> delegate) {
		_mousePressDelegate = std::move(delegate);
	}
	void handleMousePress(QPoint globalPosition);

	void setMouseReleaseDelegate(Fn<void(QPoint globalPosition)> delegate) {
		_mouseReleaseDelegate = std::move(delegate);
	}
	void handleMouseRelease(QPoint globalPosition);

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	class Action;

	void updateSelected(QPoint globalPosition);
	void init();

	not_null<QAction*> addAction(not_null<QAction*> action, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);

	void setSelected(int selected);
	void clearMouseSelection();

	void itemPressed(TriggeredSource source);

	ItemBase *findSelectedAction() const;

	const style::Menu &_st;

	Fn<void(QAction *action, int actionTop, TriggeredSource source)> _activatedCallback;
	Fn<void(QAction *action, int actionTop, TriggeredSource source)> _triggeredCallback;
	Fn<bool(int key)> _keyPressDelegate;
	Fn<void(QPoint globalPosition)> _mouseMoveDelegate;
	Fn<void(QPoint globalPosition)> _mousePressDelegate;
	Fn<void(QPoint globalPosition)> _mouseReleaseDelegate;

	QMenu *_wappedMenu = nullptr;
	std::vector<not_null<QAction*>> _actions;
	std::vector<base::unique_qptr<ItemBase>> _actionWidgets;

	int _forceWidth = 0;

	bool _mouseSelection = false;

	int _selected = -1;
	bool _childShown = false;

};

} // namespace Ui
