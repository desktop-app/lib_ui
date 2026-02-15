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
#include <rpl/variable.h>
#include <rpl/distinct_until_changed.h>

#include <QtWidgets/QWidget>
#include <QtCore/QPointer>
#include <QtGui/QtEvents>
#include <QAccessible>

namespace Ui {

class RpWidget;

void ToggleChildrenVisibility(not_null<QWidget*> widget, bool visible);

void ResizeFitChild(
	not_null<RpWidget*> parent,
	not_null<RpWidget*> child,
	int heightMin = 0);

template <typename Widget, typename Traits>
class RpWidgetBase;

class RpWidgetWrap {
public:
	virtual ~RpWidgetWrap() = default;

	[[nodiscard]] virtual QWidget *rpWidget() = 0;
	[[nodiscard]] virtual const QWidget *rpWidget() const = 0;

	[[nodiscard]] rpl::producer<not_null<QEvent*>> events() const;
	[[nodiscard]] rpl::producer<QRect> geometryValue() const;
	[[nodiscard]] rpl::producer<QSize> sizeValue() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<int> widthValue() const;
	[[nodiscard]] rpl::producer<QPoint> positionValue() const;
	[[nodiscard]] rpl::producer<int> leftValue() const;
	[[nodiscard]] rpl::producer<int> topValue() const;
	[[nodiscard]] virtual rpl::producer<int> desiredHeightValue() const;
	[[nodiscard]] rpl::producer<bool> shownValue() const;
	[[nodiscard]] rpl::producer<not_null<QScreen*>> screenValue() const;
	[[nodiscard]] rpl::producer<bool> windowActiveValue() const;
	[[nodiscard]] rpl::producer<QRect> paintRequest() const;
	void paintOn(Fn<void(QPainter&)> callback);
	[[nodiscard]] rpl::producer<> alive() const;
	[[nodiscard]] rpl::producer<> death() const;
	[[nodiscard]] rpl::producer<> macWindowDeactivateEvents() const;
	[[nodiscard]] rpl::producer<WId> winIdValue() const;

	[[nodiscard]] virtual QMargins getMargins() const;
	[[nodiscard]] bool externalWidthWasSet() const;

	// Get the size of the widget as it should be.
	// Negative return value means no default width.
	[[nodiscard]] int naturalWidth() const;
	[[nodiscard]] rpl::producer<int> naturalWidthValue() const;
	void setNaturalWidth(int value);

	// Resizes content and counts natural widget height for the desired width.
	virtual int resizeGetHeight(int newWidth) {
		return heightNoMargins();
	}

	[[nodiscard]] QRect rectNoMargins() const {
		return rpWidget()->rect().marginsRemoved(getMargins());
	}

	[[nodiscard]] int widthNoMargins() const {
		return rectNoMargins().width();
	}

	[[nodiscard]] int heightNoMargins() const {
		return rectNoMargins().height();
	}

	[[nodiscard]] int bottomNoMargins() const {
		const auto g = rpWidget()->geometry().marginsRemoved(getMargins());
		return g.y() + g.height();
	}

	[[nodiscard]] QSize sizeNoMargins() const {
		return rectNoMargins().size();
	}

	template <typename Error, typename Generator>
	void showOn(rpl::producer<bool, Error, Generator> &&shown) {
		std::move(
			shown
		) | rpl::on_next([this](bool visible) {
			callSetVisible(visible);
		}, lifetime());
	}

	[[nodiscard]] rpl::lifetime &lifetime();

protected:
	bool handleEvent(QEvent *event);
	virtual bool eventHook(QEvent *event) = 0;

private:
	template <typename Widget, typename Traits>
	friend class RpWidgetBase;

	static constexpr auto kNaturalWidthAny = uint32(0x7FFFFFFF);

	struct EventStreams {
		rpl::event_stream<not_null<QEvent*>> events;
		rpl::event_stream<QRect> geometry;
		rpl::event_stream<QRect> paint;
		rpl::event_stream<bool> shown;
		rpl::event_stream<not_null<QScreen*>> screen;
		rpl::event_stream<int> naturalWidthChanges;
		rpl::event_stream<bool> windowActive;
		rpl::event_stream<WId> winId;
		rpl::event_stream<> alive;
		uint32 naturalWidth : 31 = kNaturalWidthAny;
		uint32 externalWidthWasSet : 1 = 0;
	};
	struct Initer {
		Initer(QWidget *parent, bool setZeroGeometry);
	};

	virtual void callSetVisible(bool visible) = 0;
	virtual void callResizeToNaturalWidth() = 0;

	void visibilityChangedHook(bool wasVisible, bool nowVisible);
	[[nodiscard]] EventStreams &eventStreams() const;

	mutable std::unique_ptr<EventStreams> _eventStreams;
	rpl::lifetime _lifetime;

};

struct RpWidgetDefaultTraits {
	static constexpr bool kSetZeroGeometry = true;
};

template <typename Widget, typename Traits = RpWidgetDefaultTraits>
class RpWidgetBase : public Widget, public RpWidgetWrap {
	using Self = RpWidgetBase<Widget, Traits>;

public:
	using Widget::Widget;

	~RpWidgetBase() {
		base::take(_lifetime);
		base::take(_eventStreams);
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
		Widget::move(
			(style::RightToLeft()
				? ((outerw > 0
					? outerw
					: Widget::parentWidget()->width())
					- x
					- Widget::width())
				: x),
			y);
	}
	void moveToRight(int x, int y, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.right();
		y -= margins.top();
		Widget::move(
			(style::RightToLeft()
				? x
				: ((outerw > 0
					? outerw
					: Widget::parentWidget()->width())
					- x
					- Widget::width())),
			y);
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
		Widget::setGeometry(
			(style::RightToLeft()
				? ((outerw > 0
					? outerw
					: Widget::parentWidget()->width()) - x - w)
				: x),
			y,
			w,
			h);
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
		Widget::setGeometry(
			(style::RightToLeft()
				? x
				: ((outerw > 0
					? outerw
					: Widget::parentWidget()->width()) - x - w)),
			y,
			w,
			h);
	}
	[[nodiscard]] QPoint myrtlpoint(int x, int y) const {
		return style::rtlpoint(x, y, Widget::width());
	}
	[[nodiscard]] QPoint myrtlpoint(const QPoint point) const {
		return style::rtlpoint(point, Widget::width());
	}
	[[nodiscard]] QRect myrtlrect(int x, int y, int w, int h) const {
		return style::rtlrect(x, y, w, h, Widget::width());
	}
	[[nodiscard]] QRect myrtlrect(const QRect &rect) const {
		return style::rtlrect(rect, Widget::width());
	}
	void rtlupdate(const QRect &rect) {
		Widget::update(myrtlrect(rect));
	}
	void rtlupdate(int x, int y, int w, int h) {
		Widget::update(myrtlrect(x, y, w, h));
	}

	[[nodiscard]] QPoint mapFromGlobal(const QPoint &point) const {
		return Widget::mapFromGlobal(point);
	}
	[[nodiscard]] QPoint mapToGlobal(const QPoint &point) const {
		return Widget::mapToGlobal(point);
	}
	[[nodiscard]] QRect mapFromGlobal(const QRect &rect) const {
		return QRect(mapFromGlobal(rect.topLeft()), rect.size());
	}
	[[nodiscard]] QRect mapToGlobal(const QRect &rect) const {
		return QRect(mapToGlobal(rect.topLeft()), rect.size());
	}

	[[nodiscard]] QWidget *rpWidget() final {
		return this;
	}
	[[nodiscard]] const QWidget *rpWidget() const final {
		return this;
	}
	void setVisible(bool visible) final {
		auto wasVisible = !this->isHidden();
		setVisibleHook(visible);
		visibilityChangedHook(wasVisible, !this->isHidden());
	}

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth, bool internal = false) {
		if (!internal) {
			eventStreams().externalWidthWasSet = 1;
		}
		const auto margins = getMargins();
		const auto fullWidth = margins.left() + newWidth + margins.right();
		const auto fullHeight = margins.top()
			+ resizeGetHeight(newWidth)
			+ margins.bottom();
		const auto newSize = QSize(fullWidth, fullHeight);
		if (newSize != Widget::size()) {
			Widget::resize(newSize);
			Widget::update();
		}
	}

protected:
	bool event(QEvent *event) final {
		return handleEvent(event);
	}
	bool eventHook(QEvent *event) override {
		return Widget::event(event);
	}
	virtual void setVisibleHook(bool visible) {
		Widget::setVisible(visible);
	}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	void enterEvent(QEnterEvent *e) final {
		if (auto parent = rparent()) {
			parent->leaveToChildEvent(e, this);
		}
		return enterEventHook(e);
	}
#else // Qt >= 6.0.0
	void enterEvent(QEvent *e) final {
		if (auto parent = rparent()) {
			parent->leaveToChildEvent(e, this);
		}
		return enterEventHook(static_cast<QEnterEvent*>(e));
	}
#endif // Qt < 6.0.0
	virtual void enterEventHook(QEnterEvent *e) {
		return Widget::enterEvent(e);
	}

	void leaveEvent(QEvent *e) final {
		if (auto parent = rparent()) {
			parent->enterFromChildEvent(e, this);
		}
		return leaveEventHook(e);
	}
	virtual void leaveEventHook(QEvent *e) {
		return Widget::leaveEvent(e);
	}

private:
	void callSetVisible(bool visible) final {
		Self::setVisible(visible); // Save one virtual method invocation.
	}
	void callResizeToNaturalWidth() final {
		const auto natural = naturalWidth();
		resizeToWidth((natural >= 0) ? natural : widthNoMargins(), true);
	}

	[[nodiscard]] RpWidget *rparent() {
		return qobject_cast<RpWidget*>(Widget::parentWidget());
	}

	Initer _initer = { this, Traits::kSetZeroGeometry };

};

// Add required fields from QAccessible::State when necessary.
// Don't forget to amend the AccessibilityState::writeTo implementation.
// This one allows universal initialization, like { .checkable = true }.
struct AccessibilityState {
	bool checkable : 1 = false;
	bool checked : 1 = false;
	bool pressed : 1 = false;
	bool readOnly : 1 = false;
	bool selected : 1 = false;
	// Use -1 to mean "don't override", 0 for false, 1 for true.
	int focused : 2 = -1;
	int focusable : 2 = -1;

	void writeTo(QAccessible::State &state);
};

class RpWidget : public RpWidgetBase<QWidget> {
	// The Q_OBJECT meta info is used for qobject_cast above!
	Q_OBJECT

public:
	explicit RpWidget(QWidget *parent = nullptr);

	// Resize to minimum of natural width and available width.
	void resizeToNaturalWidth(int newWidth) {
		const auto natural = naturalWidth();
		resizeToWidth((natural >= 0) ? qMin(newWidth, natural) : newWidth);
	}

	// Updates the area that is visible inside the scroll container.
	void setVisibleTopBottom(int visibleTop, int visibleBottom) {
		const auto max = std::max(height(), 0);
		visibleTopBottomUpdated(
			std::clamp(visibleTop, 0, max),
			std::clamp(visibleBottom, 0, max));
	}

	[[nodiscard]] virtual QAccessibleInterface *accessibilityCreate();
	[[nodiscard]] virtual QAccessible::Role accessibilityRole();
	[[nodiscard]] virtual QString accessibilityName();
	void accessibilityNameChanged();
	[[nodiscard]] virtual QString accessibilityDescription();
	void accessibilityDescriptionChanged();
	[[nodiscard]] virtual AccessibilityState accessibilityState() const;
	void accessibilityStateChanged(AccessibilityState changes);
	[[nodiscard]] virtual QString accessibilityValue() const;
	void accessibilityValueChanged();
	[[nodiscard]] virtual QStringList accessibilityActionNames();
	virtual void accessibilityDoAction(const QString &name);
	[[nodiscard]] virtual int accessibilityChildCount() const;
	[[nodiscard]] virtual RpWidget *accessibilityParent() const;
	[[nodiscard]] virtual QAccessibleInterface* accessibilityChildInterface(int index) const;
	[[nodiscard]] virtual QString accessibilityChildName(int index) const;
	void accessibilityChildNameChanged(int index);
	[[nodiscard]] virtual QString accessibilityChildDescription(int index) const;
	void accessibilityChildDescriptionChanged(int index);
	[[nodiscard]] virtual QString accessibilityChildValue(int index) const;
	void accessibilityChildValueChanged(int index);
	[[nodiscard]] virtual QAccessible::State accessibilityChildState(int index) const;
	void accessibilityChildStateChanged(int index, AccessibilityState changes);
	[[nodiscard]] virtual QAccessible::Role accessibilityChildRole() const;
	[[nodiscard]] virtual QRect accessibilityChildRect(int index) const;
	[[nodiscard]] virtual int accessibilityChildColumnCount(int row) const;
	[[nodiscard]] virtual QAccessible::Role accessibilityChildSubItemRole() const;
	[[nodiscard]] virtual QString accessibilityChildSubItemName(int row, int column) const;
	[[nodiscard]] virtual QString accessibilityChildSubItemValue(int row, int column) const;
	void accessibilityChildFocused(int index);

protected:
	// e - from enterEvent() of child RpWidget
	virtual void leaveToChildEvent(QEvent *e, QWidget *child) {
	}

	// e - from leaveEvent() of child RpWidget
	virtual void enterFromChildEvent(QEvent *e, QWidget *child) {
	}

	void setChildVisibleTopBottom(
			RpWidget *child,
			int visibleTop,
			int visibleBottom) {
		if (child) {
			auto top = child->y();
			child->setVisibleTopBottom(
				visibleTop - top,
				visibleBottom - top);
		}
	}

	virtual void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	}

	template <typename OtherWidget, typename OtherTraits>
	friend class RpWidgetBase;

};

struct VisibleRange {
	int top = 0;
	int bottom = 0;

	friend inline bool operator==(VisibleRange, VisibleRange) = default;
};
class VisibleRangeWidget final : public RpWidget {
public:
	using RpWidget::RpWidget;

	[[nodiscard]] rpl::producer<VisibleRange> visibleRange() const {
		return _visibleRange.value();
	}
private:
	void visibleTopBottomUpdated(
			int visibleTop,
			int visibleBottom) override {
		_visibleRange = VisibleRange{ visibleTop, visibleBottom };
	}

	rpl::variable<VisibleRange> _visibleRange;

};

} // namespace Ui
