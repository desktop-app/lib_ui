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

namespace style {
struct MenuSeparator;
} // namespace style

namespace Ui {

class ScrollArea;

class PopupMenu : public RpWidget {
public:
	enum class VerticalOrigin {
		Top,
		Bottom,
	};

	enum class AnimatePhase {
		Hidden,
		StartShow,
		Shown,
		StartHide,
	};

	PopupMenu(QWidget *parent, const style::PopupMenu &st = st::defaultPopupMenu);
	PopupMenu(QWidget *parent, QMenu *menu, const style::PopupMenu &st = st::defaultPopupMenu);
	~PopupMenu();

	[[nodiscard]] const style::PopupMenu &st() const {
		return _st;
	}
	[[nodiscard]] QRect inner() const {
		return _inner;
	}
	[[nodiscard]] rpl::producer<AnimatePhase> animatePhaseValue() const {
		return _animatePhase.value();
	}

	not_null<QAction*> addAction(base::unique_qptr<Menu::ItemBase> widget);
	not_null<QAction*> addAction(
		const QString &text,
		Fn<void()> callback,
		const style::icon *icon = nullptr,
		const style::icon *iconOver = nullptr);
	not_null<QAction*> addAction(
		const QString &text,
		std::unique_ptr<PopupMenu> submenu,
		const style::icon *icon = nullptr,
		const style::icon *iconOver = nullptr);
	not_null<QAction*> addSeparator(
		const style::MenuSeparator *st = nullptr);
	not_null<QAction*> insertAction(
		int position,
		base::unique_qptr<Menu::ItemBase> widget);
	void clearActions();

	[[nodiscard]] const std::vector<not_null<QAction*>> &actions() const;
	[[nodiscard]] not_null<PopupMenu*> ensureSubmenu(
		not_null<QAction*> action,
		const style::PopupMenu &st);
	void removeSubmenu(not_null<QAction*> action);
	void checkSubmenuShow();
	bool empty() const;

	void deleteOnHide(bool del);
	void popup(const QPoint &p);
	bool prepareGeometryFor(const QPoint &p);
	void popupPrepared();
	void hideMenu(bool fast = false);
	void setTopShift(int topShift);
	void setForceWidth(int forceWidth);
	void setForcedOrigin(PanelAnimation::Origin origin);
	void setForcedVerticalOrigin(VerticalOrigin origin);
	void setAdditionalMenuPadding(QMargins padding, QMargins margins);

	[[nodiscard]] PanelAnimation::Origin preparedOrigin() const;
	[[nodiscard]] QMargins preparedPadding() const;
	[[nodiscard]] QMargins preparedMargins() const;
	[[nodiscard]] bool useTransparency() const;

	[[nodiscard]] int scrollTop() const;
	[[nodiscard]] rpl::producer<int> scrollTopValue() const;

	void setDestroyedCallback(Fn<void()> callback) {
		_destroyedCallback = std::move(callback);
	}
	void discardParentReActivate() {
		_reactivateParent = false;
	}

	[[nodiscard]] not_null<Menu::Menu*> menu() const {
		return _menu;
	}

	struct ShowState {
		float64 opacity = 1.;
		float64 widthProgress = 1.;
		float64 heightProgress = 1.;
		int appearingWidth = 0;
		int appearingHeight = 0;
		bool appearing = false;
		bool toggling = false;
	};
	[[nodiscard]] rpl::producer<ShowState> showStateValue() const;

	void setClearLastSeparator(bool clear);

protected:
	void paintEvent(QPaintEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

	bool eventFilter(QObject *o, QEvent *e) override;

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
	void fireCurrentShowState();

	using TriggeredSource = Menu::TriggeredSource;
	void validateCompositingSupport();
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
	bool prepareGeometryFor(const QPoint &p, PopupMenu *parent);
	void showPrepared(TriggeredSource source);
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
	QMargins _padding;
	QMargins _margins;
	QMargins _additionalMenuPadding;
	QMargins _additionalMenuMargins;

	QPointer<PopupMenu> _activeSubmenu;

	std::optional<VerticalOrigin> _forcedVerticalOrigin;
	PanelAnimation::Origin _origin = PanelAnimation::Origin::TopLeft;
	std::optional<PanelAnimation::Origin> _forcedOrigin;
	std::unique_ptr<PanelAnimation> _showAnimation;
	Animations::Simple _a_show;
	rpl::event_stream<ShowState> _showStateChanges;
	rpl::variable<AnimatePhase> _animatePhase = AnimatePhase::Hidden;

	bool _useTransparency = true;
	bool _hiding = false;
	QPixmap _cache;
	Animations::Simple _a_opacity;

	bool _deleteOnHide = true;
	bool _triggering = false;
	bool _deleteLater = false;
	bool _reactivateParent = true;
	bool _grabbingForPanelAnimation = false;

	int _topShift = 0;
	bool _clearLastSeparator = true;

	Fn<void()> _destroyedCallback;

};

} // namespace Ui
