// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"
#include "base/flags.h"
#include "ui/layers/layer_widget.h"
#include "ui/layers/box_content.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/effects/animation_value.h"
#include "ui/text/text_entity.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

class Painter;

namespace style {
struct RoundButton;
struct IconButton;
struct ScrollArea;
struct Box;
} // namespace style

namespace Ui {

class RoundButton;
class IconButton;
class ScrollArea;
class FlatLabel;
class FadeShadow;

class BoxLayerWidget : public LayerWidget, public BoxContentDelegate {
public:
	BoxLayerWidget(
		not_null<LayerStackWidget*> layer,
		object_ptr<BoxContent> content);
	~BoxLayerWidget();

	void parentResized() override;

	void setLayerType(bool layerType) override;
	void setStyle(const style::Box &st) override;
	const style::Box &style() override;
	void setTitle(rpl::producer<TextWithEntities> title) override;
	void setAdditionalTitle(rpl::producer<QString> additional) override;
	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) override;

	void showFinished() override {
		_content->showFinished();
	}

	void clearButtons() override;
	QPointer<RoundButton> addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) override;
	QPointer<RoundButton> addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) override;
	QPointer<IconButton> addTopButton(
		const style::IconButton &st,
		Fn<void()> clickCallback) override;
	void showLoading(bool show) override;
	void updateButtonsPositions() override;
	QPointer<QWidget> outerContainer() override;

	void setDimensions(
		int newWidth,
		int maxHeight,
		bool forceCenterPosition = false) override;

	void setNoContentMargin(bool noContentMargin) override {
		if (_noContentMargin != noContentMargin) {
			_noContentMargin = noContentMargin;
			updateSize();
		}
	}

	bool isBoxShown() const override {
		return !isHidden();
	}
	void closeBox() override {
		closeLayer();
	}
	void triggerButton(int index) override;

	void setCloseByOutsideClick(bool close) override;
	bool closeByOutsideClick() const override;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void doSetInnerFocus() override {
		_content->setInnerFocus();
	}
	void closeHook() override {
		_content->notifyBoxClosing();
	}

private:
	struct LoadingProgress;

	void paintAdditionalTitle(Painter &p);
	void updateTitlePosition();

	[[nodiscard]] const style::Box &st() const;
	[[nodiscard]] bool hasTitle() const;
	[[nodiscard]] int titleHeight() const;
	[[nodiscard]] int buttonsHeight() const;
	[[nodiscard]] int buttonsTop() const;
	[[nodiscard]] int contentTop() const;
	[[nodiscard]] int countFullHeight() const;
	[[nodiscard]] int countRealHeight() const;
	[[nodiscard]] QRect loadingRect() const;
	void updateSize();

	const style::Box *_st = nullptr;
	not_null<LayerStackWidget*> _layer;
	int _fullHeight = 0;

	bool _noContentMargin = false;
	int _maxContentHeight = 0;
	object_ptr<BoxContent> _content;

	RoundRect _roundRect;
	object_ptr<FlatLabel> _title = { nullptr };
	Fn<TextWithEntities()> _titleFactory;
	rpl::variable<QString> _additionalTitle;
	int _titleLeft = 0;
	int _titleTop = 0;
	bool _layerType = false;
	bool _closeByOutsideClick = true;

	std::vector<object_ptr<RoundButton>> _buttons;
	object_ptr<RoundButton> _leftButton = { nullptr };
	base::unique_qptr<IconButton> _topButton = { nullptr };
	std::unique_ptr<LoadingProgress> _loadingProgress;

};

} // namespace Ui
