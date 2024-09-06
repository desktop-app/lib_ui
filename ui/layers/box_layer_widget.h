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
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

class Painter;
struct TextWithEntities;

namespace anim {
enum class type : uchar;
} // namespace anim

namespace style {
struct Box;
} // namespace style

namespace Ui {

class AbstractButton;
class FlatLabel;

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

	void setCustomCornersFilling(RectParts corners) override;
	void clearButtons() override;
	void addButton(object_ptr<AbstractButton> button) override;
	void addLeftButton(object_ptr<AbstractButton> button) override;
	void addTopButton(object_ptr<AbstractButton> button) override;
	void showLoading(bool show) override;
	void updateButtonsPositions() override;
	ShowFactory showFactory() override;
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
	void hideLayer() override;
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
	bool _layerType = false;
	int _fullHeight = 0;

	bool _noContentMargin = false;
	int _maxContentHeight = 0;
	object_ptr<BoxContent> _content;

	RoundRect _roundRect;
	object_ptr<FlatLabel> _title = { nullptr };
	Fn<TextWithEntities()> _titleFactory;
	rpl::variable<QString> _additionalTitle;
	RectParts _customCornersFilling;
	int _titleLeft = 0;
	int _titleTop = 0;
	bool _closeByOutsideClick = true;

	std::vector<object_ptr<AbstractButton>> _buttons;
	object_ptr<AbstractButton> _leftButton = { nullptr };
	base::unique_qptr<AbstractButton> _topButton = { nullptr };
	std::unique_ptr<LoadingProgress> _loadingProgress;

};

} // namespace Ui
