// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"
#include "base/flags.h"

#include <optional>

namespace Window {
class SectionMemento;
struct SectionShow;
} // namespace Window

namespace style {
struct Box;
} // namespace style

namespace Ui {

class BoxContent;

enum class LayerOption {
	CloseOther = (1 << 0),
	KeepOther = (1 << 1),
	ShowAfterOther = (1 << 2),
};
using LayerOptions = base::flags<LayerOption>;
inline constexpr auto is_flag_type(LayerOption) { return true; };

class Show;
using ShowPtr = std::shared_ptr<Show>;
using ShowFactory = Fn<ShowPtr()>;

// Delegate the BoxLayerWidget queries for higher-level layer-stack concerns:
// style overrides, nested box showing, layer hiding, popup outer container.
// Implemented by LayerStackWidget for embedded usage and by standalone
// controllers that want to show each box as its own top-level window.
class LayerStackDelegate {
public:
	virtual ~LayerStackDelegate() = default;

	[[nodiscard]] virtual const style::Box *boxStyleOverride() const {
		return nullptr;
	}
	[[nodiscard]] virtual const style::Box *boxStyleOverrideLayer() const {
		return nullptr;
	}

	virtual void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) = 0;
	virtual void hideLayers(anim::type animated) = 0;
	[[nodiscard]] virtual ShowFactory showFactory() = 0;

	// Returned by BoxLayerWidget::outerContainer() unless null — then the
	// BoxLayerWidget returns its own parent (or itself when standalone).
	[[nodiscard]] virtual QPointer<QWidget> layerOuterContainer() {
		return nullptr;
	}

	// Size of the outer container the box positions within. Used by
	// BoxLayerWidget to cap the box's real height. nullopt = use
	// parentWidget()->size() (or no cap if no parent).
	[[nodiscard]] virtual std::optional<QSize> layerOuterSize() {
		return std::nullopt;
	}

	// When true, the BoxLayerWidget centers / repositions itself within
	// its parent on setDimensions / parentResized. When false (standalone
	// mode) the box only resizes itself to its natural size and the
	// outer container (e.g. a SeparatePanel wrapper) follows it.
	[[nodiscard]] virtual bool centerWithinOuter() {
		return true;
	}

	// When true, BoxLayerWidget treats mouse presses in its title area
	// (excluding top buttons) as a window drag — calls startSystemMove()
	// on its containing top-level QWindow.
	[[nodiscard]] virtual bool dragByTitle() {
		return false;
	}

};

class LayerWidget : public RpWidget {
public:
	using RpWidget::RpWidget;

	virtual void parentResized() = 0;
	virtual void showFinished() {
	}
	void setInnerFocus();
	bool setClosing() {
		if (!_closing) {
			_closing = true;
			closeHook();
			return true;
		}
		return false;
	}

	bool overlaps(const QRect &globalRect);

	void setClosedCallback(Fn<void()> callback) {
		_closedCallback = std::move(callback);
	}
	void setResizedCallback(Fn<void()> callback) {
		_resizedCallback = std::move(callback);
	}
	virtual bool takeToThirdSection() {
		return false;
	}
	virtual bool showSectionInternal(
			not_null<::Window::SectionMemento*> memento,
			const ::Window::SectionShow &params) {
		return false;
	}
	virtual bool closeByOutsideClick() const {
		return true;
	}
	virtual bool closeByBackButton() {
		closeLayer();
		return true;
	}
	[[nodiscard]] virtual crl::time animationDuration() const {
		return 0;
	}

	void closeLayer() {
		if (const auto callback = base::take(_closedCallback)) {
			callback();
		}
	}

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	bool focusNextPrevChild(bool next) override;

	virtual void doSetInnerFocus() {
		setFocus();
	}
	virtual void closeHook() {
	}

private:
	bool _closing = false;
	Fn<void()> _closedCallback;
	Fn<void()> _resizedCallback;

};

class LayerStackWidget : public RpWidget, public LayerStackDelegate {
public:
	LayerStackWidget(QWidget *parent, ShowFactory showFactory);

	void finishAnimating();
	rpl::producer<> hideFinishEvents() const;

	void setStyleOverrides(
		const style::Box *boxSt,
		const style::Box *layerSt);
	[[nodiscard]] const style::Box *boxStyleOverrideLayer() const override {
		return _layerSt;
	}
	[[nodiscard]] const style::Box *boxStyleOverride() const override {
		return _boxSt;
	}
	[[nodiscard]] ShowFactory showFactory() override {
		return _showFactory;
	}
	[[nodiscard]] QPointer<QWidget> layerOuterContainer() override {
		return this;
	}

	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) override;
	void showLayer(
		std::unique_ptr<LayerWidget> layer,
		LayerOptions options,
		anim::type animated);
	void showSpecialLayer(
		object_ptr<LayerWidget> layer,
		anim::type animated);
	void showMainMenu(
		object_ptr<LayerWidget> menu,
		anim::type animated);
	bool takeToThirdSection();

	bool canSetFocus() const;
	void setInnerFocus();

	bool contentOverlapped(const QRect &globalRect);

	void hideSpecialLayer(anim::type animated);
	void hideLayers(anim::type animated) override;
	void hideAll(anim::type animated);
	void hideTopLayer(anim::type animated);
	bool closeCurrentByBackButton();
	void setHideByBackgroundClick(bool hide);
	void removeBodyCache();

	// If you need to divide animated hideAll().
	void hideAllAnimatedPrepare();
	void hideAllAnimatedRun();

	bool showSectionInternal(
		not_null<::Window::SectionMemento*> memento,
		const ::Window::SectionShow &params);

	bool layerShown() const;
	bool boxShown() const;
	[[nodiscard]] rpl::producer<bool> boxShownValue() const;
	const LayerWidget *topShownLayer() const;

	~LayerStackWidget();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void appendLayer(
		std::unique_ptr<LayerWidget> layer,
		anim::type animated);
	void prependLayer(
		std::unique_ptr<LayerWidget> layer,
		anim::type animated);
	void replaceLayer(
		std::unique_ptr<LayerWidget> layer,
		anim::type animated);
	void backgroundClicked();

	LayerWidget *pushLayer(
		std::unique_ptr<LayerWidget> layer,
		anim::type animated);
	void showFinished();
	void hideCurrent(anim::type animated);
	void closeLayer(not_null<LayerWidget*> layer);

	enum class Action {
		ShowMainMenu,
		ShowSpecialLayer,
		ShowLayer,
		HideSpecialLayer,
		HideLayer,
		HideAll,
	};
	template <typename SetupNew, typename ClearOld>
	bool prepareAnimation(
		SetupNew &&setupNewWidgets,
		ClearOld &&clearOldWidgets,
		Action action,
		anim::type animated,
		crl::time duration);
	template <typename SetupNew, typename ClearOld>
	void startAnimation(
		SetupNew &&setupNewWidgets,
		ClearOld &&clearOldWidgets,
		Action action,
		anim::type animated,
		crl::time duration = 0);

	void prepareForAnimation();
	void animationDone();

	void setCacheImages();
	void updateBoxShown();
	void clearLayers();
	void clearSpecialLayer();
	void initChildLayer(LayerWidget *layer);
	void updateLayerBoxes();
	void fixOrder();
	void sendFakeMouseEvent();
	void clearClosingLayers();

	LayerWidget *currentLayer() {
		return _layers.empty() ? nullptr : _layers.back().get();
	}
	const LayerWidget *currentLayer() const {
		return const_cast<LayerStackWidget*>(this)->currentLayer();
	}

	std::vector<std::unique_ptr<LayerWidget>> _layers;
	std::vector<std::unique_ptr<LayerWidget>> _closingLayers;

	object_ptr<LayerWidget> _specialLayer = { nullptr };
	object_ptr<LayerWidget> _mainMenu = { nullptr };

	class BackgroundWidget;
	object_ptr<BackgroundWidget> _background;

	ShowFactory _showFactory;

	const style::Box *_boxSt = nullptr;
	const style::Box *_layerSt = nullptr;
	bool _hideByBackgroundClick = true;

	rpl::event_stream<> _hideFinishStream;
	rpl::variable<bool> _boxShown = false;

};

} // namespace Ui
