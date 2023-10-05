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

#include <QtWidgets/QMenu>

namespace Ui {
struct ScrollToRequest;
} // namespace Ui

namespace style {
struct Menu;
struct MenuSeparator;
} // namespace style

namespace st {
extern const style::Menu &defaultMenu;
} // namespace st

namespace Ui::Menu {

class ItemBase;
class RippleAnimation;

class Menu : public RpWidget {
public:
	Menu(QWidget *parent, const style::Menu &st = st::defaultMenu);
	Menu(QWidget *parent, QMenu *menu, const style::Menu &st = st::defaultMenu);
	~Menu();

	[[nodiscard]] const style::Menu &st() const {
		return _st;
	}

	not_null<QAction*> addAction(base::unique_qptr<ItemBase> widget);
	not_null<QAction*> addAction(
		const QString &text,
		Fn<void()> callback,
		const style::icon *icon = nullptr,
		const style::icon *iconOver = nullptr);
	not_null<QAction*> addAction(
		const QString &text,
		std::unique_ptr<QMenu> submenu,
		const style::icon *icon = nullptr,
		const style::icon *iconOver = nullptr);
	not_null<QAction*> addSeparator(
		const style::MenuSeparator *st = nullptr);
	not_null<QAction*> insertAction(
		int position,
		base::unique_qptr<ItemBase> widget);
	void clearActions();
	void clearLastSeparator();
	void finishAnimating();

	bool empty() const;

	void clearSelection();

	void setChildShownAction(QAction *action) {
		_childShownAction = action;
	}
	void setShowSource(TriggeredSource source);
	void setForceWidth(int forceWidth);

	const std::vector<not_null<QAction*>> &actions() const;

	void setActivatedCallback(Fn<void(const CallbackData &data)> callback) {
		_activatedCallback = std::move(callback);
	}
	void setTriggeredCallback(Fn<void(const CallbackData &data)> callback) {
		_triggeredCallback = std::move(callback);
	}

	[[nodiscard]] ItemBase *findSelectedAction() const;

	void setKeyPressDelegate(Fn<bool(int key)> delegate) {
		_keyPressDelegate = std::move(delegate);
	}
	void handleKeyPress(not_null<QKeyEvent*> e);

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

	void setSelected(int selected, bool isMouseSelection);

	[[nodiscard]] rpl::producer<> resizesFromInner() const;
	[[nodiscard]] rpl::producer<ScrollToRequest> scrollToRequests() const;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void updateSelected(QPoint globalPosition);
	void init();

	not_null<QAction*> addAction(
		not_null<QAction*> action,
		const style::icon *icon = nullptr,
		const style::icon *iconOver = nullptr);

	void clearMouseSelection();

	void itemPressed(TriggeredSource source);

	void resizeFromInner(int w, int h);

	const style::Menu &_st;

	Fn<void(const CallbackData &data)> _activatedCallback;
	Fn<void(const CallbackData &data)> _triggeredCallback;
	Fn<bool(int key)> _keyPressDelegate;
	Fn<void(QPoint globalPosition)> _mouseMoveDelegate;
	Fn<void(QPoint globalPosition)> _mousePressDelegate;
	Fn<void(QPoint globalPosition)> _mouseReleaseDelegate;

	QMenu *_wappedMenu = nullptr;
	std::vector<not_null<QAction*>> _actions;
	std::vector<base::unique_qptr<ItemBase>> _actionWidgets;

	int _forceWidth = 0;
	bool _lastSelectedByMouse = false;

	QPointer<QAction> _childShownAction;

	rpl::event_stream<> _resizesFromInner;
	rpl::event_stream<ScrollToRequest> _scrollToRequests;

};

} // namespace Ui::Menu
