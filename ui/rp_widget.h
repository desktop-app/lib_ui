// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"
#include "ui/style/style_core_direction.h"

#include <rpl/event_stream.h>
#include <rpl/map.h>
#include <rpl/distinct_until_changed.h>

#include <QtWidgets/QWidget>
#include <QtCore/QPointer>
#include <QtGui/QtEvents>

namespace Ui {

void ToggleChildrenVisibility(not_null<QWidget*> widget, bool visible);

} // namespace Ui

class TWidget;

template <typename Base>
class TWidgetHelper : public Base {
public:
	using Base::Base;

	virtual QMargins getMargins() const {
		return QMargins();
	}

	void hideChildren() {
		Ui::ToggleChildrenVisibility(this, false);
	}
	void showChildren() {
		Ui::ToggleChildrenVisibility(this, true);
	}

	void moveToLeft(int x, int y, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.left();
		y -= margins.top();
		Base::move(style::RightToLeft() ? ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - Base::width()) : x, y);
	}
	void moveToRight(int x, int y, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.right();
		y -= margins.top();
		Base::move(style::RightToLeft() ? x : ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - Base::width()), y);
	}
	void setGeometryToLeft(const QRect &r, int outerw = 0) {
		setGeometryToLeft(r.x(), r.y(), r.width(), r.height(), outerw);
	}
	void setGeometryToLeft(int x, int y, int w, int h, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.left();
		y -= margins.top();
		w -= margins.left() - margins.right();
		h -= margins.top() - margins.bottom();
		Base::setGeometry(style::RightToLeft() ? ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - w) : x, y, w, h);
	}
	void setGeometryToRight(const QRect &r, int outerw = 0) {
		setGeometryToRight(r.x(), r.y(), r.width(), r.height(), outerw);
	}
	void setGeometryToRight(int x, int y, int w, int h, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.right();
		y -= margins.top();
		w -= margins.left() - margins.right();
		h -= margins.top() - margins.bottom();
		Base::setGeometry(style::RightToLeft() ? x : ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - w), y, w, h);
	}
	QPoint myrtlpoint(int x, int y) const {
		return style::rtlpoint(x, y, Base::width());
	}
	QPoint myrtlpoint(const QPoint point) const {
		return style::rtlpoint(point, Base::width());
	}
	QRect myrtlrect(int x, int y, int w, int h) const {
		return style::rtlrect(x, y, w, h, Base::width());
	}
	QRect myrtlrect(const QRect &rect) const {
		return style::rtlrect(rect, Base::width());
	}
	void rtlupdate(const QRect &rect) {
		Base::update(myrtlrect(rect));
	}
	void rtlupdate(int x, int y, int w, int h) {
		Base::update(myrtlrect(x, y, w, h));
	}

	QPoint mapFromGlobal(const QPoint &point) const {
		return Base::mapFromGlobal(point);
	}
	QPoint mapToGlobal(const QPoint &point) const {
		return Base::mapToGlobal(point);
	}
	QRect mapFromGlobal(const QRect &rect) const {
		return QRect(mapFromGlobal(rect.topLeft()), rect.size());
	}
	QRect mapToGlobal(const QRect &rect) const {
		return QRect(mapToGlobal(rect.topLeft()), rect.size());
	}

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	void enterEvent(QEnterEvent *e) final override {
		if (auto parent = tparent()) {
			parent->leaveToChildEvent(e, this);
		}
		return enterEventHook(e);
	}
#else // Qt >= 6.0.0
	void enterEvent(QEvent *e) final override {
		if (auto parent = tparent()) {
			parent->leaveToChildEvent(e, this);
		}
		return enterEventHook(static_cast<QEnterEvent*>(e));
	}
#endif // Qt < 6.0.0
	virtual void enterEventHook(QEnterEvent *e) {
		return Base::enterEvent(e);
	}

	void leaveEvent(QEvent *e) final override {
		if (auto parent = tparent()) {
			parent->enterFromChildEvent(e, this);
		}
		return leaveEventHook(e);
	}
	virtual void leaveEventHook(QEvent *e) {
		return Base::leaveEvent(e);
	}

	// e - from enterEvent() of child TWidget
	virtual void leaveToChildEvent(QEvent *e, QWidget *child) {
	}

	// e - from leaveEvent() of child TWidget
	virtual void enterFromChildEvent(QEvent *e, QWidget *child) {
	}

private:
	TWidget *tparent() {
		return qobject_cast<TWidget*>(Base::parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(Base::parentWidget());
	}

	template <typename OtherBase>
	friend class TWidgetHelper;

};

class TWidget : public TWidgetHelper<QWidget> {
	// The Q_OBJECT meta info is used for qobject_cast!
	Q_OBJECT

public:
	TWidget(QWidget *parent = nullptr);

	// Get the size of the widget as it should be.
	// Negative return value means no default width.
	virtual int naturalWidth() const {
		return -1;
	}

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth) {
		auto margins = getMargins();
		auto fullWidth = margins.left() + newWidth + margins.right();
		auto fullHeight = margins.top() + resizeGetHeight(newWidth) + margins.bottom();
		auto newSize = QSize(fullWidth, fullHeight);
		if (newSize != size()) {
			resize(newSize);
			update();
		}
	}

	// Resize to minimum of natural width and available width.
	void resizeToNaturalWidth(int newWidth) {
		const auto natural = naturalWidth();
		resizeToWidth((natural >= 0) ? qMin(newWidth, natural) : newWidth);
	}

	QRect rectNoMargins() const {
		return rect().marginsRemoved(getMargins());
	}

	int widthNoMargins() const {
		return rectNoMargins().width();
	}

	int heightNoMargins() const {
		return rectNoMargins().height();
	}

	int bottomNoMargins() const {
		auto rectWithoutMargins = rectNoMargins();
		return y() + rectWithoutMargins.y() + rectWithoutMargins.height();
	}

	QSize sizeNoMargins() const {
		return rectNoMargins().size();
	}

	// Updates the area that is visible inside the scroll container.
	void setVisibleTopBottom(int visibleTop, int visibleBottom) {
		const auto max = std::max(height(), 0);
		visibleTopBottomUpdated(
			std::clamp(visibleTop, 0, max),
			std::clamp(visibleBottom, 0, max));
	}

protected:
	void setChildVisibleTopBottom(
			TWidget *child,
			int visibleTop,
			int visibleBottom) {
		if (child) {
			auto top = child->y();
			child->setVisibleTopBottom(
				visibleTop - top,
				visibleBottom - top);
		}
	}

	// Resizes content and counts natural widget height for the desired width.
	virtual int resizeGetHeight(int newWidth) {
		return heightNoMargins();
	}

	virtual void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	}

};

namespace Ui {

class RpWidget;

void ResizeFitChild(
	not_null<RpWidget*> parent,
	not_null<RpWidget*> child,
	int heightMin = 0);

template <typename Widget>
using RpWidgetParent = std::conditional_t<
	std::is_same_v<Widget, QWidget>,
	TWidget,
	TWidgetHelper<Widget>>;

template <typename Widget, typename Traits>
class RpWidgetBase;

class RpWidgetWrap {
public:
	virtual QWidget *rpWidget() = 0;
	virtual const QWidget *rpWidget() const = 0;

	rpl::producer<not_null<QEvent*>> events() const;
	rpl::producer<QRect> geometryValue() const;
	rpl::producer<QSize> sizeValue() const;
	rpl::producer<int> heightValue() const;
	rpl::producer<int> widthValue() const;
	rpl::producer<QPoint> positionValue() const;
	rpl::producer<int> leftValue() const;
	rpl::producer<int> topValue() const;
	virtual rpl::producer<int> desiredHeightValue() const;
	rpl::producer<bool> shownValue() const;
	rpl::producer<not_null<QScreen*>> screenValue() const;
	rpl::producer<bool> windowActiveValue() const;
	rpl::producer<QRect> paintRequest() const;
	rpl::producer<> alive() const;
	rpl::producer<> death() const;
	rpl::producer<> macWindowDeactivateEvents() const;
	rpl::producer<WId> winIdValue() const;

	template <typename Error, typename Generator>
	void showOn(rpl::producer<bool, Error, Generator> &&shown) {
		std::move(
			shown
		) | rpl::start_with_next([this](bool visible) {
			callSetVisible(visible);
		}, lifetime());
	}

	rpl::lifetime &lifetime();

	virtual ~RpWidgetWrap() = default;

protected:
	bool handleEvent(QEvent *event);
	virtual bool eventHook(QEvent *event) = 0;

private:
	template <typename Widget, typename Traits>
	friend class RpWidgetBase;

	struct EventStreams {
		rpl::event_stream<not_null<QEvent*>> events;
		rpl::event_stream<QRect> geometry;
		rpl::event_stream<QRect> paint;
		rpl::event_stream<bool> shown;
		rpl::event_stream<not_null<QScreen*>> screen;
		rpl::event_stream<bool> windowActive;
		rpl::event_stream<WId> winId;
		rpl::event_stream<> alive;
	};
	struct Initer {
		Initer(QWidget *parent, bool setZeroGeometry);
	};

	virtual void callSetVisible(bool visible) = 0;

	void visibilityChangedHook(bool wasVisible, bool nowVisible);
	EventStreams &eventStreams() const;

	mutable std::unique_ptr<EventStreams> _eventStreams;
	rpl::lifetime _lifetime;

};

struct RpWidgetDefaultTraits {
	static constexpr bool kSetZeroGeometry = true;
};

template <typename Widget, typename Traits = RpWidgetDefaultTraits>
class RpWidgetBase
	: public RpWidgetParent<Widget>
	, public RpWidgetWrap {
	using Self = RpWidgetBase<Widget, Traits>;
	using Parent = RpWidgetParent<Widget>;

public:
	using Parent::Parent;

	QWidget *rpWidget() final override {
		return this;
	}
	const QWidget *rpWidget() const final override {
		return this;
	}
	void setVisible(bool visible) final override {
		auto wasVisible = !this->isHidden();
		setVisibleHook(visible);
		visibilityChangedHook(wasVisible, !this->isHidden());
	}

	~RpWidgetBase() {
		base::take(_lifetime);
		base::take(_eventStreams);
	}

protected:
	bool event(QEvent *event) final override {
		return handleEvent(event);
	}
	bool eventHook(QEvent *event) override {
		return Parent::event(event);
	}
	virtual void setVisibleHook(bool visible) {
		Parent::setVisible(visible);
	}

private:
	void callSetVisible(bool visible) final override {
		Self::setVisible(visible); // Save one virtual method invocation.
	}

	Initer _initer = { this, Traits::kSetZeroGeometry };

};

class RpWidget : public RpWidgetBase<QWidget> {
public:
	using RpWidgetBase<QWidget>::RpWidgetBase;

};

} // namespace Ui
