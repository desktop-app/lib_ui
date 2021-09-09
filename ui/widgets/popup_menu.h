// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "styles/style_widgets.h"
#include "ui/widgets/menu/menu.h"
#include "ui/effects/animations.h"
#include "ui/effects/panel_animation.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"
#include "base/unique_qptr.h"

namespace Ui {

class ScrollArea;

class PopupMenu : public RpWidget {
public:
	PopupMenu(QWidget *parent, const style::PopupMenu &st = st::defaultPopupMenu);
	PopupMenu(QWidget *parent, QMenu *menu, const style::PopupMenu &st = st::defaultPopupMenu);

	[[nodiscard]] const style::PopupMenu &st() const {
		return _st;
	}

	not_null<QAction*> addAction(base::unique_qptr<Menu::ItemBase> widget);
	not_null<QAction*> addAction(const QString &text, Fn<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	not_null<QAction*> addAction(const QString &text, std::unique_ptr<PopupMenu> submenu);
	not_null<QAction*> addSeparator();
	void clearActions();

	[[nodiscard]] const std::vector<not_null<QAction*>> &actions() const;
	[[nodiscard]] not_null<PopupMenu*> ensureSubmenu(
		not_null<QAction*> action);
	void removeSubmenu(not_null<QAction*> action);
	void checkSubmenuShow();
	bool empty() const;

	void deleteOnHide(bool del);
	void popup(const QPoint &p);
	void hideMenu(bool fast = false);
	void setForcedOrigin(PanelAnimation::Origin origin);

	void setDestroyedCallback(Fn<void()> callback) {
		_destroyedCallback = std::move(callback);
	}
	void discardParentReActivate() {
		_reactivateParent = false;
	}

	[[nodiscard]] not_null<Menu::Menu*> menu() const {
		return _menu;
	}

	~PopupMenu();

protected:
	void paintEvent(QPaintEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void paintBg(QPainter &p);
	void hideFast();
	void setOrigin(PanelAnimation::Origin origin);
	void showAnimated(PanelAnimation::Origin origin);
	void hideAnimated();

	QImage grabForPanelAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();
	void childHiding(PopupMenu *child);

	void showAnimationCallback();
	void opacityAnimationCallback();

	void init();

	void hideFinished();
	void showStarted();

	using TriggeredSource = Menu::TriggeredSource;
	void handleCompositingUpdate();
	void handleMenuResize();
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

	bool popupSubmenuFromAction(const Menu::CallbackData &data);
	void popupSubmenu(
		not_null<QAction*> action,
		not_null<PopupMenu*> submenu,
		int actionTop,
		TriggeredSource source);
	void showMenu(const QPoint &p, PopupMenu *parent, TriggeredSource source);
	void updateRoundingOverlay();

	const style::PopupMenu &_st;

	RoundRect _roundRect;
	object_ptr<ScrollArea> _scroll;
	not_null<Menu::Menu*> _menu;
	object_ptr<RpWidget> _roundingOverlay = { nullptr };

	base::flat_map<
		not_null<QAction*>,
		base::unique_qptr<PopupMenu>> _submenus;

	PopupMenu *_parent = nullptr;

	QRect _inner;
	style::margins _padding;

	QPointer<PopupMenu> _activeSubmenu;

	PanelAnimation::Origin _origin = PanelAnimation::Origin::TopLeft;
	std::optional<PanelAnimation::Origin> _forcedOrigin;
	std::unique_ptr<PanelAnimation> _showAnimation;
	Animations::Simple _a_show;

	bool _useTransparency = true;
	bool _hiding = false;
	QPixmap _cache;
	Animations::Simple _a_opacity;

	bool _deleteOnHide = true;
	bool _triggering = false;
	bool _deleteLater = false;
	bool _reactivateParent = true;
	bool _grabbingForPanelAnimation = false;

	Fn<void()> _destroyedCallback;

};

} // namespace Ui
