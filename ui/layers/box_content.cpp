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
#include "ui/rect_part.h"
#include "ui/painter.h"
#include "base/timer.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Ui {
namespace {

class BoxShow final : public Show {
public:
	explicit BoxShow(not_null<Ui::BoxContent*> box);
	~BoxShow();

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

private:
	BoxShow(QPointer<BoxContent> weak, ShowPtr wrapped);

	bool resolve() const;

	const QPointer<Ui::BoxContent> _weak;
	mutable std::shared_ptr<Show> _wrapped;
	rpl::lifetime _lifetime;

};

BoxShow::BoxShow(not_null<BoxContent*> box)
: BoxShow(MakeWeak(box.get()), nullptr) {
}

BoxShow::BoxShow(QPointer<BoxContent> weak, ShowPtr wrapped)
: _weak(weak)
, _wrapped(std::move(wrapped)) {
	if (!resolve()) {
		if (const auto box = _weak.data()) {
			box->boxClosing(
			) | rpl::start_with_next([=] {
				resolve();
				_lifetime.destroy();
			}, _lifetime);
		}
	}
}

BoxShow::~BoxShow() = default;

bool BoxShow::resolve() const {
	if (_wrapped) {
		return true;
	} else if (const auto strong = _weak.data()) {
		if (strong->hasDelegate()) {
			_wrapped = strong->getDelegate()->showFactory()();
			return true;
		}
	}
	return false;
}

void BoxShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const {
	if (resolve()) {
		_wrapped->showOrHideBoxOrLayer(std::move(layer), options, animated);
	}
}

not_null<QWidget*> BoxShow::toastParent() const {
	if (resolve()) {
		return _wrapped->toastParent();
	}
	Unexpected("Stale BoxShow::toastParent call.");
}

bool BoxShow::valid() const {
	return resolve() && _wrapped->valid();
}

BoxShow::operator bool() const {
	return valid();
}

} // namespace

void BoxContent::setTitle(rpl::producer<QString> title) {
	getDelegate()->setTitle(std::move(title) | Text::ToWithEntities());
}

QPointer<AbstractButton> BoxContent::addButton(
		object_ptr<AbstractButton> button) {
	auto result = QPointer<AbstractButton>(button.data());
	getDelegate()->addButton(std::move(button));
	return result;
}

QPointer<RoundButton> BoxContent::addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback) {
	return addButton(
		std::move(text),
		std::move(clickCallback),
		getDelegate()->style().button);
}

QPointer<RoundButton> BoxContent::addButton(
		rpl::producer<QString> text,
		const style::RoundButton &st) {
	return addButton(std::move(text), nullptr, st);
}

QPointer<RoundButton> BoxContent::addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) {
	auto button = object_ptr<RoundButton>(this, std::move(text), st);
	auto result = QPointer<RoundButton>(button.data());
	result->setTextTransform(RoundButton::TextTransform::NoTransform);
	result->setClickedCallback(std::move(clickCallback));
	getDelegate()->addButton(std::move(button));
	return result;
}

QPointer<AbstractButton> BoxContent::addLeftButton(
		object_ptr<AbstractButton> button) {
	auto result = QPointer<AbstractButton>(button.data());
	getDelegate()->addLeftButton(std::move(button));
	return result;
}

QPointer<RoundButton> BoxContent::addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback) {
	return addLeftButton(
		std::move(text),
		std::move(clickCallback),
		getDelegate()->style().button);
}

QPointer<RoundButton> BoxContent::addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) {
	auto button = object_ptr<RoundButton>(this, std::move(text), st);
	const auto result = QPointer<RoundButton>(button.data());
	result->setTextTransform(RoundButton::TextTransform::NoTransform);
	result->setClickedCallback(std::move(clickCallback));
	getDelegate()->addLeftButton(std::move(button));
	return result;
}

QPointer<AbstractButton> BoxContent::addTopButton(
		object_ptr<AbstractButton> button) {
	auto result = QPointer<AbstractButton>(button.data());
	getDelegate()->addTopButton(std::move(button));
	return result;
}

QPointer<IconButton> BoxContent::addTopButton(
		const style::IconButton &st,
		Fn<void()> clickCallback) {
	auto button = object_ptr<IconButton>(this, st);
	const auto result = QPointer<IconButton>(button.data());
	result->setClickedCallback(std::move(clickCallback));
	getDelegate()->addTopButton(std::move(button));
	return result;
}

void BoxContent::setInner(
		object_ptr<TWidget> inner,
		const style::ScrollArea &st) {
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
	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		updateInnerVisibleTopBottom();
		updateShadowsVisibility();
	}, lifetime());
	_scroll->innerResizes(
	) | rpl::start_with_next([=] {
		updateInnerVisibleTopBottom();
		updateShadowsVisibility();
	}, lifetime());
	_draggingScroll.scrolls(
	) | rpl::start_with_next([=](int delta) {
		if (_scroll) {
			_scroll->scrollToY(_scroll->scrollTop() + delta);
		}
	}, lifetime());
}

void BoxContent::scrollToWidget(not_null<QWidget*> widget) {
	if (_scroll) {
		_scroll->scrollToWidget(widget);
	}
}

RectParts BoxContent::customCornersFilling() {
	return {};
}

void BoxContent::scrollToY(int top, int bottom) {
	scrollTo({ top, bottom });
}

void BoxContent::scrollTo(ScrollToRequest request, anim::type animated) {
	if (_scroll) {
		const auto v = _scroll->computeScrollTo(request.ymin, request.ymax);
		const auto now = _scroll->scrollTop();
		if (animated == anim::type::instant || v == now) {
			_scrollAnimation.stop();
			_scroll->scrollToY(v);
		} else {
			_scrollAnimation.start([=] {
				_scroll->scrollToY(_scrollAnimation.value(v));
			}, now, v, st::slideWrapDuration, anim::sineInOut);
		}
	}
}

void BoxContent::sendScrollViewportEvent(not_null<QEvent*> event) {
	if (_scroll) {
		_scroll->viewportEvent(event);
	}
}

rpl::producer<> BoxContent::scrolls() const {
	return _scroll ? _scroll->scrolls() : rpl::never<>();
}

int BoxContent::scrollTop() const {
	return _scroll ? _scroll->scrollTop() : 0;
}

int BoxContent::scrollHeight() const {
	return _scroll ? _scroll->height() : 0;
}

base::weak_ptr<Toast::Instance> BoxContent::showToast(
		Toast::Config &&config) {
	return BoxShow(this).showToast(std::move(config));
}

base::weak_ptr<Toast::Instance> BoxContent::showToast(
		TextWithEntities &&text,
		crl::time duration) {
	return BoxShow(this).showToast(std::move(text), duration);
}

base::weak_ptr<Toast::Instance> BoxContent::showToast(
		const QString &text,
		crl::time duration) {
	return BoxShow(this).showToast(text, duration);
}

std::shared_ptr<Show> BoxContent::uiShow() {
	return std::make_shared<BoxShow>(this);
}

void BoxContent::scrollByDraggingDelta(int delta) {
	_draggingScroll.checkDeltaScroll(_scroll ? delta : 0);
}

void BoxContent::updateInnerVisibleTopBottom() {
	const auto widget = static_cast<TWidget*>(_scroll
		? _scroll->widget()
		: nullptr);
	if (widget) {
		const auto top = _scroll->scrollTop();
		widget->setVisibleTopBottom(top, top + _scroll->height());
	}
}

void BoxContent::updateShadowsVisibility(anim::type animated) {
	if (!_scroll) {
		return;
	}

	const auto top = _scroll->scrollTop();
	_topShadow->toggle(
		((top > 0)
			|| (_innerTopSkip > 0
				&& !getDelegate()->style().shadowIgnoreTopSkip)),
		animated);
	_bottomShadow->toggle(
		(top < _scroll->scrollTopMax() || _innerBottomSkip > 0),
		animated);
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
		const auto delta = innerTopSkip - _innerTopSkip;
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
	const auto isTopShadowVisible = !_topShadow->isHidden();
	const auto isBottomShadowVisible = !_bottomShadow->isHidden();
	if (isTopShadowVisible) {
		_topShadow->setVisible(false);
	}
	if (isBottomShadowVisible) {
		_bottomShadow->setVisible(false);
	}
	const auto result = GrabWidget(this, _scroll->geometry());
	if (isTopShadowVisible) {
		_topShadow->setVisible(true);
	}
	if (isBottomShadowVisible) {
		_bottomShadow->setVisible(true);
	}
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
	const auto newScrollHeight = height() - _innerTopSkip - _innerBottomSkip;
	const auto changed = (_scroll->height() != newScrollHeight);
	_scroll->setGeometryToLeft(0, _innerTopSkip, width(), newScrollHeight);
	_topShadow->entity()->resize(width(), st::lineWidth);
	_topShadow->moveToLeft(0, _innerTopSkip);
	_bottomShadow->entity()->resize(width(), st::lineWidth);
	_bottomShadow->moveToLeft(
		0,
		height() - _innerBottomSkip - st::lineWidth);
	if (changed) {
		updateInnerVisibleTopBottom();
		updateShadowsVisibility(anim::type::instant);
	}
}

object_ptr<TWidget> BoxContent::doTakeInnerWidget() {
	return _scroll->takeWidget<TWidget>();
}

void BoxContent::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (testAttribute(Qt::WA_OpaquePaintEvent)) {
		const auto &color = getDelegate()->style().bg;
		for (const auto &rect : e->region()) {
			p.fillRect(rect, color);
		}
	}
}

} // namespace Ui
