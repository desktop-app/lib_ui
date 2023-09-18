// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/layers/box_layer_widget.h"

#include "ui/effects/radial_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "base/integration.h"
#include "base/timer.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Ui {

struct BoxLayerWidget::LoadingProgress {
	LoadingProgress(
		Fn<void()> &&callback,
		const style::InfiniteRadialAnimation &st);

	InfiniteRadialAnimation animation;
	base::Timer removeTimer;
};

BoxLayerWidget::LoadingProgress::LoadingProgress(
	Fn<void()> &&callback,
	const style::InfiniteRadialAnimation &st)
: animation(std::move(callback), st) {
}

BoxLayerWidget::BoxLayerWidget(
	not_null<LayerStackWidget*> layer,
	object_ptr<BoxContent> content)
: LayerWidget(layer)
, _layer(layer)
, _content(std::move(content))
, _roundRect(st::boxRadius, st().bg) {
	_content->setParent(this);
	_content->setDelegate(this);

	const auto &object = *_content;
	base::Integration::Instance().setCrashAnnotation(
		"BoxName",
		QString::fromUtf8(typeid(object).name()));

	_additionalTitle.changes(
	) | rpl::start_with_next([=] {
		updateSize();
		update();
	}, lifetime());
}

BoxLayerWidget::~BoxLayerWidget() = default;

void BoxLayerWidget::setLayerType(bool layerType) {
	if (_layerType == layerType) {
		return;
	}
	_layerType = layerType;
	updateTitlePosition();
	if (_maxContentHeight) {
		setDimensions(width(), _maxContentHeight);
	}
}

int BoxLayerWidget::titleHeight() const {
	return st::boxTitleHeight;
}

const style::Box &BoxLayerWidget::st() const {
	return _st
		? *_st
		: _layerType
		? (_layer->boxStyleOverrideLayer()
			? *_layer->boxStyleOverrideLayer()
			: st::layerBox)
		: (_layer->boxStyleOverride()
			? *_layer->boxStyleOverride()
			: st::defaultBox);
}

void BoxLayerWidget::setStyle(const style::Box &st) {
	_st = &st;
	_roundRect.setColor(st.bg);
}

const style::Box &BoxLayerWidget::style() {
	return st();
}

int BoxLayerWidget::buttonsHeight() const {
	const auto padding = st().buttonPadding;
	return padding.top() + st().buttonHeight + padding.bottom();
}

int BoxLayerWidget::buttonsTop() const {
	const auto padding = st().buttonPadding;
	return height() - padding.bottom() - st().buttonHeight;
}

QRect BoxLayerWidget::loadingRect() const {
	const auto padding = st().buttonPadding;
	const auto size = st::boxLoadingSize;
	const auto skipx = st::boxTitlePosition.x();
	const auto skipy = (st().buttonHeight - size) / 2;
	return QRect(
		skipx,
		height() - padding.bottom() - skipy - size,
		size,
		size);
}

void BoxLayerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto custom = _content->customCornersFilling();
	const auto clip = e->rect();
	const auto paintTopRounded = !(custom & RectPart::FullTop)
		&& clip.intersects(QRect(0, 0, width(), st::boxRadius));
	const auto paintBottomRounded = !(custom & RectPart::FullBottom)
		&& clip.intersects(
			QRect(0, height() - st::boxRadius, width(), st::boxRadius));
	if (paintTopRounded || paintBottomRounded) {
		_roundRect.paint(p, rect(), RectPart::None
			| (paintTopRounded ? RectPart::FullTop : RectPart::None)
			| (paintBottomRounded ? RectPart::FullBottom : RectPart::None));
	}
	const auto other = e->region().intersected(
		QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius));
	if (!other.isEmpty()) {
		for (const auto &rect : other) {
			p.fillRect(rect, st().bg);
		}
	}
	if (!_additionalTitle.current().isEmpty()
		&& clip.intersects(QRect(0, 0, width(), titleHeight()))) {
		paintAdditionalTitle(p);
	}
	if (_loadingProgress) {
		const auto rect = loadingRect();
		_loadingProgress->animation.draw(
			p,
			rect.topLeft(),
			rect.size(),
			width());
	}
}

void BoxLayerWidget::paintAdditionalTitle(Painter &p) {
	p.setFont(st::boxTitleAdditionalFont);
	p.setPen(st().titleAdditionalFg);
	p.drawTextLeft(
		_titleLeft + (_title ? _title->width() : 0) + st::boxTitleAdditionalSkip,
		_titleTop + st::boxTitleFont->ascent - st::boxTitleAdditionalFont->ascent,
		width(),
		_additionalTitle.current());
}

void BoxLayerWidget::parentResized() {
	auto newHeight = countRealHeight();
	auto parentSize = parentWidget()->size();
	setGeometry(
		(parentSize.width() - width()) / 2,
		(parentSize.height() - newHeight) / 2,
		width(),
		newHeight);
	update();
}

void BoxLayerWidget::setTitle(rpl::producer<TextWithEntities> title) {
	const auto wasTitle = hasTitle();
	if (title) {
		_title.create(this, rpl::duplicate(title), st().title);
		_title->show();
		std::move(
			title
		) | rpl::start_with_next([=] {
			updateTitlePosition();
		}, _title->lifetime());
	} else {
		_title.destroy();
	}
	if (wasTitle != hasTitle()) {
		updateSize();
	}
}

void BoxLayerWidget::setAdditionalTitle(rpl::producer<QString> additional) {
	_additionalTitle = std::move(additional);
}

void BoxLayerWidget::triggerButton(int index) {
	if (index < _buttons.size()) {
		_buttons[index]->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
	}
}

void BoxLayerWidget::setCloseByOutsideClick(bool close) {
	_closeByOutsideClick = close;
}

bool BoxLayerWidget::closeByOutsideClick() const {
	return _closeByOutsideClick;
}

bool BoxLayerWidget::hasTitle() const {
	return (_title != nullptr) || !_additionalTitle.current().isEmpty();
}

void BoxLayerWidget::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	_layer->showBox(std::move(box), options, animated);
}

void BoxLayerWidget::hideLayer() {
	_layer->hideLayers(anim::type::normal);
}

void BoxLayerWidget::updateSize() {
	setDimensions(width(), _maxContentHeight);
}

void BoxLayerWidget::updateButtonsPositions() {
	if (!_buttons.empty() || _leftButton) {
		auto padding = st().buttonPadding;
		auto right = padding.right();
		auto top = buttonsTop();
		if (_leftButton) {
			_leftButton->moveToLeft(right, top);
		}
		for (const auto &button : _buttons) {
			button->moveToRight(right, top);
			right += button->width() + padding.left();
		}
	}
	if (_topButton) {
		_topButton->moveToRight(0, 0);
	}
}

ShowFactory BoxLayerWidget::showFactory() {
	return _layer->showFactory();
}

QPointer<QWidget> BoxLayerWidget::outerContainer() {
	return parentWidget();
}

void BoxLayerWidget::updateTitlePosition() {
	_titleLeft = st::boxTitlePosition.x();
	_titleTop = st::boxTitlePosition.y();
	if (_title) {
		const auto topButtonSkip = _topButton
			? (_topButton->width() / 2)
			: 0;
		_title->resizeToNaturalWidth(
			width() - _titleLeft * 2 - topButtonSkip);
		_title->moveToLeft(_titleLeft, _titleTop);
	}
}

void BoxLayerWidget::clearButtons() {
	for (auto &button : base::take(_buttons)) {
		button.destroy();
	}
	_leftButton.destroy();
	_topButton = nullptr;
}

void BoxLayerWidget::addButton(object_ptr<AbstractButton> button) {
	_buttons.push_back(std::move(button));
	const auto raw = _buttons.back().data();
	raw->setParent(this);
	raw->show();
	raw->widthValue(
	) | rpl::start_with_next([=] {
		updateButtonsPositions();
	}, raw->lifetime());
}

void BoxLayerWidget::addLeftButton(object_ptr<AbstractButton> button) {
	_leftButton = std::move(button);
	const auto raw = _leftButton.data();
	raw->setParent(this);
	raw->show();
	raw->widthValue(
	) | rpl::start_with_next([=] {
		updateButtonsPositions();
	}, raw->lifetime());
}

void BoxLayerWidget::addTopButton(object_ptr<AbstractButton> button) {
	_topButton = base::unique_qptr<AbstractButton>(button.release());
	const auto raw = _topButton.get();
	raw->setParent(this);
	raw->show();
	updateButtonsPositions();
	updateTitlePosition();
}

void BoxLayerWidget::showLoading(bool show) {
	const auto &st = st::boxLoadingAnimation;
	if (!show) {
		if (_loadingProgress && !_loadingProgress->removeTimer.isActive()) {
			_loadingProgress->removeTimer.callOnce(
				st.sineDuration + st.sinePeriod);
			_loadingProgress->animation.stop();
		}
		return;
	}
	if (!_loadingProgress) {
		const auto callback = [=] {
			if (!anim::Disabled()) {
				const auto t = st::boxLoadingAnimation.thickness;
				update(loadingRect().marginsAdded({ t, t, t, t }));
			}
		};
		_loadingProgress = std::make_unique<LoadingProgress>(
			callback,
			st::boxLoadingAnimation);
		_loadingProgress->removeTimer.setCallback([=] {
			_loadingProgress = nullptr;
		});
	} else {
		_loadingProgress->removeTimer.cancel();
	}
	_loadingProgress->animation.start();
}


void BoxLayerWidget::setDimensions(int newWidth, int maxHeight, bool forceCenterPosition) {
	_maxContentHeight = maxHeight;

	auto fullHeight = countFullHeight();
	if (width() != newWidth || _fullHeight != fullHeight) {
		_fullHeight = fullHeight;
		if (parentWidget()) {
			auto oldGeometry = geometry();
			resize(newWidth, countRealHeight());
			auto newGeometry = geometry();
			auto parentHeight = parentWidget()->height();
			const auto bottomMargin = st().margin.bottom();
			if (newGeometry.top() + newGeometry.height() + bottomMargin > parentHeight
				|| forceCenterPosition) {
				const auto top1 = parentHeight - bottomMargin - newGeometry.height();
				const auto top2 = (parentHeight - newGeometry.height()) / 2;
				const auto newTop = forceCenterPosition
					? std::min(top1, top2)
					: std::max(top1, top2);
				if (newTop != newGeometry.top()) {
					move(newGeometry.left(), newTop);
					resizeEvent(0);
				}
			}
			parentWidget()->update(oldGeometry.united(geometry()).marginsAdded(st::boxRoundShadow.extend));
		} else {
			resize(newWidth, 0);
		}
	}
}

int BoxLayerWidget::countRealHeight() const {
	const auto &margin = st().margin;
	return std::min(
		_fullHeight,
		parentWidget()->height() - margin.top() - margin.bottom());
}

int BoxLayerWidget::countFullHeight() const {
	return contentTop() + _maxContentHeight + buttonsHeight();
}

int BoxLayerWidget::contentTop() const {
	return hasTitle()
		? titleHeight()
		: _noContentMargin
		?
		0
		: st::boxTopMargin;
}

void BoxLayerWidget::resizeEvent(QResizeEvent *e) {
	updateButtonsPositions();
	updateTitlePosition();

	const auto top = contentTop();
	_content->resize(width(), height() - top - buttonsHeight());
	_content->moveToLeft(0, top);

	LayerWidget::resizeEvent(e);
}

void BoxLayerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		closeBox();
	} else {
		LayerWidget::keyPressEvent(e);
	}
}

} // namespace Ui
