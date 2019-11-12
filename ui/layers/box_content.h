// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"
#include "base/flags.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/layers/layer_widget.h"
#include "ui/effects/animation_value.h"
#include "ui/text/text_entity.h"
#include "ui/rp_widget.h"

namespace style {
struct RoundButton;
struct IconButton;
struct ScrollArea;
struct Box;
} // namespace style

namespace Ui {
class GenericBox;
} // namespace Ui

template <typename BoxType = Ui::GenericBox, typename ...Args>
inline object_ptr<BoxType> Box(Args &&...args) {
	const auto parent = static_cast<QWidget*>(nullptr);
	return object_ptr<BoxType>(parent, std::forward<Args>(args)...);
}

namespace Ui {

class RoundButton;
class IconButton;
class ScrollArea;
class FlatLabel;
class FadeShadow;
class BoxContent;

class BoxContentDelegate {
public:
	virtual void setLayerType(bool layerType) = 0;
	virtual void setStyle(const style::Box &st) = 0;
	virtual void setTitle(rpl::producer<TextWithEntities> title) = 0;
	virtual void setAdditionalTitle(rpl::producer<QString> additional) = 0;
	virtual void setCloseByOutsideClick(bool close) = 0;

	virtual void clearButtons() = 0;
	virtual QPointer<RoundButton> addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) = 0;
	virtual QPointer<RoundButton> addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) = 0;
	virtual QPointer<IconButton> addTopButton(
		const style::IconButton &st,
		Fn<void()> clickCallback) = 0;
	virtual void showLoading(bool show) = 0;
	virtual void updateButtonsPositions() = 0;

	virtual void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) = 0;
	virtual void setDimensions(
		int newWidth,
		int maxHeight,
		bool forceCenterPosition = false) = 0;
	virtual void setNoContentMargin(bool noContentMargin) = 0;
	virtual bool isBoxShown() const = 0;
	virtual void closeBox() = 0;
	virtual void triggerButton(int index) = 0;

	template <typename BoxType>
	QPointer<BoxType> show(
			object_ptr<BoxType> content,
			LayerOptions options = LayerOption::KeepOther,
			anim::type animated = anim::type::normal) {
		auto result = QPointer<BoxType>(content.data());
		showBox(std::move(content), options, animated);
		return result;
	}

	virtual QPointer<QWidget> outerContainer() = 0;

};

class BoxContent : public RpWidget {
	Q_OBJECT

public:
	BoxContent() {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	bool isBoxShown() const {
		return getDelegate()->isBoxShown();
	}
	void closeBox() {
		getDelegate()->closeBox();
	}
	void triggerButton(int index) {
		getDelegate()->triggerButton(index);
	}

	void setTitle(rpl::producer<QString> title);
	void setTitle(rpl::producer<TextWithEntities> title) {
		getDelegate()->setTitle(std::move(title));
	}
	void setAdditionalTitle(rpl::producer<QString> additional) {
		getDelegate()->setAdditionalTitle(std::move(additional));
	}
	void setCloseByEscape(bool close) {
		_closeByEscape = close;
	}
	void setCloseByOutsideClick(bool close) {
		getDelegate()->setCloseByOutsideClick(close);
	}

	void scrollToWidget(not_null<QWidget*> widget);

	void clearButtons() {
		getDelegate()->clearButtons();
	}
	QPointer<RoundButton> addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback = nullptr);
	QPointer<RoundButton> addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback = nullptr);
	QPointer<IconButton> addTopButton(
			const style::IconButton &st,
			Fn<void()> clickCallback = nullptr) {
		return getDelegate()->addTopButton(st, std::move(clickCallback));
	}
	QPointer<RoundButton> addButton(
			rpl::producer<QString> text,
			const style::RoundButton &st) {
		return getDelegate()->addButton(std::move(text), nullptr, st);
	}
	QPointer<RoundButton> addButton(
			rpl::producer<QString> text,
			Fn<void()> clickCallback,
			const style::RoundButton &st) {
		return getDelegate()->addButton(
			std::move(text),
			std::move(clickCallback),
			st);
	}
	void showLoading(bool show) {
		getDelegate()->showLoading(show);
	}
	void updateButtonsGeometry() {
		getDelegate()->updateButtonsPositions();
	}
	void setStyle(const style::Box &st) {
		getDelegate()->setStyle(st);
	}

	virtual void setInnerFocus() {
		setFocus();
	}

	[[nodiscard]] rpl::producer<> boxClosing() const {
		return _boxClosingStream.events();
	}
	void notifyBoxClosing() {
		_boxClosingStream.fire({});
	}

	void setDelegate(not_null<BoxContentDelegate*> newDelegate) {
		_delegate = newDelegate;
		_preparing = true;
		prepare();
		finishPrepare();
	}
	[[nodiscard]] not_null<BoxContentDelegate*> getDelegate() const {
		return _delegate;
	}

	void setNoContentMargin(bool noContentMargin) {
		if (_noContentMargin != noContentMargin) {
			_noContentMargin = noContentMargin;
			setAttribute(Qt::WA_OpaquePaintEvent, !_noContentMargin);
		}
		getDelegate()->setNoContentMargin(noContentMargin);
	}

public slots:
	void onScrollToY(int top, int bottom = -1);

	void onDraggingScrollDelta(int delta);

protected:
	virtual void prepare() = 0;

	void setLayerType(bool layerType) {
		getDelegate()->setLayerType(layerType);
	}

	void setDimensions(
			int newWidth,
			int maxHeight,
			bool forceCenterPosition = false) {
		getDelegate()->setDimensions(
			newWidth,
			maxHeight,
			forceCenterPosition);
	}
	void setDimensionsToContent(
		int newWidth,
		not_null<RpWidget*> content);
	void setInnerTopSkip(int topSkip, bool scrollBottomFixed = false);
	void setInnerBottomSkip(int bottomSkip);

	template <typename Widget>
	QPointer<Widget> setInnerWidget(
			object_ptr<Widget> inner,
			const style::ScrollArea &st,
			int topSkip = 0,
			int bottomSkip = 0) {
		auto result = QPointer<Widget>(inner.data());
		setInnerTopSkip(topSkip);
		setInnerBottomSkip(bottomSkip);
		setInner(std::move(inner), st);
		return result;
	}

	template <typename Widget>
	QPointer<Widget> setInnerWidget(
			object_ptr<Widget> inner,
			int topSkip = 0,
			int bottomSkip = 0) {
		auto result = QPointer<Widget>(inner.data());
		setInnerTopSkip(topSkip);
		setInnerBottomSkip(bottomSkip);
		setInner(std::move(inner));
		return result;
	}

	template <typename Widget>
	object_ptr<Widget> takeInnerWidget() {
		return object_ptr<Widget>::fromRaw(
			static_cast<Widget*>(doTakeInnerWidget().release()));
	}

	void setInnerVisible(bool scrollAreaVisible);
	QPixmap grabInnerCache();

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private slots:
	void onScroll();
	void onInnerResize();

	void onDraggingScrollTimer();

private:
	void finishPrepare();
	void finishScrollCreate();
	void setInner(object_ptr<TWidget> inner);
	void setInner(object_ptr<TWidget> inner, const style::ScrollArea &st);
	void updateScrollAreaGeometry();
	void updateInnerVisibleTopBottom();
	void updateShadowsVisibility();
	object_ptr<TWidget> doTakeInnerWidget();

	BoxContentDelegate *_delegate = nullptr;

	bool _preparing = false;
	bool _noContentMargin = false;
	bool _closeByEscape = true;
	int _innerTopSkip = 0;
	int _innerBottomSkip = 0;
	object_ptr<ScrollArea> _scroll = { nullptr };
	object_ptr<FadeShadow> _topShadow = { nullptr };
	object_ptr<FadeShadow> _bottomShadow = { nullptr };

	object_ptr<QTimer> _draggingScrollTimer = { nullptr };
	int _draggingScrollDelta = 0;

	rpl::event_stream<> _boxClosingStream;

};

class BoxPointer {
public:
	BoxPointer() = default;
	BoxPointer(const BoxPointer &other) = default;
	BoxPointer(BoxPointer &&other) : _value(base::take(other._value)) {
	}
	BoxPointer(BoxContent *value) : _value(value) {
	}
	BoxPointer &operator=(const BoxPointer &other) {
		if (_value != other._value) {
			destroy();
			_value = other._value;
		}
		return *this;
	}
	BoxPointer &operator=(BoxPointer &&other) {
		if (_value != other._value) {
			destroy();
			_value = base::take(other._value);
		}
		return *this;
	}
	BoxPointer &operator=(BoxContent *other) {
		if (_value != other) {
			destroy();
			_value = other;
		}
		return *this;
	}
	~BoxPointer() {
		destroy();
	}

	BoxContent *get() const {
		return _value.data();
	}
	operator BoxContent*() const {
		return get();
	}
	explicit operator bool() const {
		return get();
	}
	BoxContent *operator->() const {
		return get();
	}

private:
	void destroy() {
		if (const auto value = base::take(_value)) {
			value->closeBox();
		}
	}

	QPointer<BoxContent> _value;

};

} // namespace Ui
