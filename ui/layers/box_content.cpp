// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/layers/box_content.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "base/timer.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Ui {

void BoxContent::setTitle(rpl::producer<QString> title) {
	getDelegate()->setTitle(std::move(title) | Text::ToWithEntities());
}

QPointer<RoundButton> BoxContent::addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback) {
	return addButton(
		std::move(text),
		std::move(clickCallback),
		st::defaultBoxButton);
}

QPointer<RoundButton> BoxContent::addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback) {
	return getDelegate()->addLeftButton(
		std::move(text),
		std::move(clickCallback),
		st::defaultBoxButton);
}

void BoxContent::setInner(object_ptr<TWidget> inner) {
	setInner(std::move(inner), st::boxScroll);
}

void BoxContent::setInner(object_ptr<TWidget> inner, const style::ScrollArea &st) {
	if (inner) {
		getDelegate()->setLayerType(true);
		_scroll.create(this, st);
		_scroll->setGeometryToLeft(0, _innerTopSkip, width(), 0);
		_scroll->setOwnedWidget(std::move(inner));
		if (_topShadow) {
			_topShadow->raise();
			_bottomShadow->raise();
		} else {
			_topShadow.create(this);
			_bottomShadow.create(this);
		}
		if (!_preparing) {
			// We didn't set dimensions yet, this will be called from finishPrepare();
			finishScrollCreate();
		}
	} else {
		getDelegate()->setLayerType(false);
		_scroll.destroyDelayed();
		_topShadow.destroyDelayed();
		_bottomShadow.destroyDelayed();
	}
}

void BoxContent::finishPrepare() {
	_preparing = false;
	if (_scroll) {
		finishScrollCreate();
	}
	setInnerFocus();
}

void BoxContent::finishScrollCreate() {
	Expects(_scroll != nullptr);

	if (!_scroll->isHidden()) {
		_scroll->show();
	}
	updateScrollAreaGeometry();
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_scroll, SIGNAL(innerResized()), this, SLOT(onInnerResize()));
}

void BoxContent::scrollToWidget(not_null<QWidget*> widget) {
	if (_scroll) {
		_scroll->scrollToWidget(widget);
	}
}

void BoxContent::onScrollToY(int top, int bottom) {
	if (_scroll) {
		_scroll->scrollToY(top, bottom);
	}
}

void BoxContent::scrollByDraggingDelta(int delta) {
	_draggingScrollDelta = _scroll ? delta : 0;
	if (_draggingScrollDelta) {
		if (!_draggingScrollTimer) {
			_draggingScrollTimer.create(this);
			_draggingScrollTimer->setSingleShot(false);
			connect(_draggingScrollTimer, SIGNAL(timeout()), this, SLOT(onDraggingScrollTimer()));
		}
		_draggingScrollTimer->start(15);
	} else {
		_draggingScrollTimer.destroy();
	}
}

void BoxContent::onDraggingScrollTimer() {
	auto delta = (_draggingScrollDelta > 0) ? qMin(_draggingScrollDelta * 3 / 20 + 1, int32(kMaxScrollSpeed)) : qMax(_draggingScrollDelta * 3 / 20 - 1, -int32(kMaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + delta);
}

void BoxContent::updateInnerVisibleTopBottom() {
	if (auto widget = static_cast<TWidget*>(_scroll ? _scroll->widget() : nullptr)) {
		auto top = _scroll->scrollTop();
		widget->setVisibleTopBottom(top, top + _scroll->height());
	}
}

void BoxContent::updateShadowsVisibility() {
	if (!_scroll) return;

	auto top = _scroll->scrollTop();
	_topShadow->toggle(
		(top > 0 || _innerTopSkip > 0),
		anim::type::normal);
	_bottomShadow->toggle(
		(top < _scroll->scrollTopMax() || _innerBottomSkip > 0),
		anim::type::normal);
}

void BoxContent::onScroll() {
	updateInnerVisibleTopBottom();
	updateShadowsVisibility();
}

void BoxContent::onInnerResize() {
	updateInnerVisibleTopBottom();
	updateShadowsVisibility();
}

void BoxContent::setDimensionsToContent(
		int newWidth,
		not_null<RpWidget*> content) {
	content->resizeToWidth(newWidth);
	content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(newWidth, height);
	}, content->lifetime());
}

void BoxContent::setInnerTopSkip(int innerTopSkip, bool scrollBottomFixed) {
	if (_innerTopSkip != innerTopSkip) {
		auto delta = innerTopSkip - _innerTopSkip;
		_innerTopSkip = innerTopSkip;
		if (_scroll && width() > 0) {
			auto scrollTopWas = _scroll->scrollTop();
			updateScrollAreaGeometry();
			if (scrollBottomFixed) {
				_scroll->scrollToY(scrollTopWas + delta);
			}
		}
	}
}

void BoxContent::setInnerBottomSkip(int innerBottomSkip) {
	if (_innerBottomSkip != innerBottomSkip) {
		auto delta = innerBottomSkip - _innerBottomSkip;
		_innerBottomSkip = innerBottomSkip;
		if (_scroll && width() > 0) {
			updateScrollAreaGeometry();
		}
	}
}

void BoxContent::setInnerVisible(bool scrollAreaVisible) {
	if (_scroll) {
		_scroll->setVisible(scrollAreaVisible);
	}
}

QPixmap BoxContent::grabInnerCache() {
	auto isTopShadowVisible = !_topShadow->isHidden();
	auto isBottomShadowVisible = !_bottomShadow->isHidden();
	if (isTopShadowVisible) _topShadow->setVisible(false);
	if (isBottomShadowVisible) _bottomShadow->setVisible(false);
	auto result = GrabWidget(this, _scroll->geometry());
	if (isTopShadowVisible) _topShadow->setVisible(true);
	if (isBottomShadowVisible) _bottomShadow->setVisible(true);
	return result;
}

void BoxContent::resizeEvent(QResizeEvent *e) {
	if (_scroll) {
		updateScrollAreaGeometry();
	}
}

void BoxContent::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && !_closeByEscape) {
		e->accept();
	} else {
		RpWidget::keyPressEvent(e);
	}
}

void BoxContent::updateScrollAreaGeometry() {
	auto newScrollHeight = height() - _innerTopSkip - _innerBottomSkip;
	auto changed = (_scroll->height() != newScrollHeight);
	_scroll->setGeometryToLeft(0, _innerTopSkip, width(), newScrollHeight);
	_topShadow->entity()->resize(width(), st::lineWidth);
	_topShadow->moveToLeft(0, _innerTopSkip);
	_bottomShadow->entity()->resize(width(), st::lineWidth);
	_bottomShadow->moveToLeft(
		0,
		height() - _innerBottomSkip - st::lineWidth);
	if (changed) {
		updateInnerVisibleTopBottom();

		auto top = _scroll->scrollTop();
		_topShadow->toggle(
			(top > 0 || _innerTopSkip > 0),
			anim::type::instant);
		_bottomShadow->toggle(
			(top < _scroll->scrollTopMax() || _innerBottomSkip > 0),
			anim::type::instant);
	}
}

object_ptr<TWidget> BoxContent::doTakeInnerWidget() {
	return _scroll->takeWidget<TWidget>();
}

void BoxContent::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (testAttribute(Qt::WA_OpaquePaintEvent)) {
		for (auto rect : e->region().rects()) {
			p.fillRect(rect, st::boxBg);
		}
	}
}

} // namespace Ui
