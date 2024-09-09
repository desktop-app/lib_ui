// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/layers/layer_widget.h"
#include "ui/text/text_entity.h"

#include <rpl/variable.h>

class Painter;

namespace style {
struct IconButton;
} // namespace style

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Ui::Toast {
struct Config;
class Instance;
} // namespace Ui::Toast

namespace Ui {

class Show;
class BoxContent;
class IconButton;
class PopupMenu;
class LayerStackWidget;
class LayerWidget;
class FlatLabel;
class InputField;
template <typename Widget>
class FadeWrapScaled;
template <typename Widget>
class FadeWrap;

struct SeparatePanelArgs {
	QWidget *parent = nullptr;
	bool onAllSpaces = false;
	Fn<bool(int zorder)> animationsPaused;
};

class SeparatePanel final : public RpWidget {
public:
	explicit SeparatePanel(SeparatePanelArgs &&args = {});
	~SeparatePanel();

	void setTitle(rpl::producer<QString> title);
	void setTitleHeight(int height);
	void setInnerSize(QSize size);
	[[nodiscard]] QRect innerGeometry() const;

	void setHideOnDeactivate(bool hideOnDeactivate);
	void showAndActivate();
	int hideGetDuration();

	[[nodiscard]] RpWidget *inner() const;
	void showInner(base::unique_qptr<RpWidget> inner);
	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated);
	void showLayer(
		std::unique_ptr<LayerWidget> layer,
		LayerOptions options,
		anim::type animated);
	void hideLayer(anim::type animated);

	[[nodiscard]] rpl::producer<> backRequests() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::producer<> closeEvents() const;
	void setBackAllowed(bool allowed);

	void updateBackToggled();

	void setMenuAllowed(Fn<void(const Menu::MenuCallback&)> fill);
	void setSearchAllowed(
		rpl::producer<QString> placeholder,
		Fn<void(std::optional<QString>)> queryChanged);
	bool closeSearch();

	void overrideTitleColor(std::optional<QColor> color);
	void overrideBottomBarColor(std::optional<QColor> color);

	base::weak_ptr<Toast::Instance> showToast(Toast::Config &&config);
	base::weak_ptr<Toast::Instance> showToast(
		TextWithEntities &&text,
		crl::time duration = 0);
	base::weak_ptr<Toast::Instance> showToast(
		const QString &text,
		crl::time duration = 0);

	[[nodiscard]] std::shared_ptr<Show> uiShow();

protected:
	void paintEvent(QPaintEvent *e) override;
	void closeEvent(QCloseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventHook(QEvent *e) override;

private:
	void initControls();
	void initLayout(const SeparatePanelArgs &args);
	void initGeometry(QSize size);
	void updateGeometry(QSize size);
	void showControls();
	void updateControlsGeometry();
	void validateBorderImage();
	[[nodiscard]] QPixmap createBorderImage(QColor color) const;
	void opacityCallback();
	void ensureLayerCreated();
	void destroyLayer();

	void updateTitleGeometry(int newWidth) const;
	void paintShadowBorder(QPainter &p) const;
	void paintOpaqueBorder(QPainter &p) const;

	void toggleOpacityAnimation(bool visible);
	void finishAnimating();
	void finishClose();

	void showMenu(Fn<void(const Menu::MenuCallback&)> fill);
	[[nodiscard]] bool createMenu(not_null<IconButton*> button);

	void updateTitleButtonColors(not_null<IconButton*> button);
	void updateTitleColors();

	void toggleSearch(bool shown);
	[[nodiscard]] rpl::producer<> allBackRequests() const;
	[[nodiscard]] rpl::producer<> allCloseRequests() const;

	object_ptr<IconButton> _close;
	object_ptr<IconButton> _menuToggle = { nullptr };
	object_ptr<FadeWrapScaled<IconButton>> _searchToggle = { nullptr };
	rpl::variable<QString> _searchPlaceholder;
	Fn<void(std::optional<QString>)> _searchQueryChanged;
	object_ptr<FadeWrap<RpWidget>> _searchWrap = { nullptr };
	InputField *_searchField = nullptr;
	object_ptr<FlatLabel> _title = { nullptr };
	object_ptr<FadeWrapScaled<IconButton>> _back;
	object_ptr<RpWidget> _body;
	base::unique_qptr<RpWidget> _inner;
	base::unique_qptr<LayerStackWidget> _layer = { nullptr };
	base::unique_qptr<PopupMenu> _menu;
	rpl::event_stream<> _synteticBackRequests;
	rpl::event_stream<> _userCloseRequests;
	rpl::event_stream<> _closeEvents;

	int _titleHeight = 0;
	bool _hideOnDeactivate = false;
	bool _useTransparency = true;
	bool _backAllowed = false;
	style::margins _padding;

	bool _dragging = false;
	QPoint _dragStartMousePosition;
	QPoint _dragStartMyPosition;

	Animations::Simple _titleLeft;
	bool _visible = false;

	Animations::Simple _opacityAnimation;
	QPixmap _animationCache;
	QPixmap _borderParts;

	std::optional<QColor> _titleOverrideColor;
	QPixmap _titleOverrideBorderParts;
	std::unique_ptr<style::palette> _titleOverridePalette;
	base::flat_map<
		not_null<IconButton*>,
		std::unique_ptr<style::IconButton>> _titleOverrideStyles;

	std::optional<QColor> _bottomBarOverrideColor;
	QPixmap _bottomBarOverrideBorderParts;

	Fn<bool(int zorder)> _animationsPaused;

};

} // namespace Ui
