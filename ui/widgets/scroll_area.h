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
#include "base/timer.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QScrollArea>
#include <QtGui/QtEvents>

namespace Ui {

// Touch flick ignore 3px.
inline constexpr auto kFingerAccuracyThreshold = 3;

// 4000px per second.
inline constexpr auto kMaxScrollAccelerated = 4000;

// 2500px per second.
inline constexpr auto kMaxScrollFlick = 2500;

enum class TouchScrollState {
	Manual, // Scrolling manually with the finger on the screen
	Auto, // Scrolling automatically
	Acceleration // Scrolling automatically but a finger is on the screen
};

class ScrollArea;

struct ScrollToRequest {
	ScrollToRequest(int ymin, int ymax)
	: ymin(ymin)
	, ymax(ymax) {
	}

	int ymin = 0;
	int ymax = 0;

};

class ScrollShadow final : public QWidget {
public:
	enum class Type {
		Top,
		Bottom,
	};
	ScrollShadow(ScrollArea *parent, const style::ScrollArea *st);

	void paintEvent(QPaintEvent *e);
	void changeVisibility(bool shown);

private:
	const style::ScrollArea *_st;

};

class ScrollBar : public TWidget {
public:
	struct ShadowVisibility {
		ScrollShadow::Type type;
		bool visible = false;
	};
	ScrollBar(ScrollArea *parent, bool vertical, const style::ScrollArea *st);

	void recountSize();
	void updateBar(bool force = false);

	void hideTimeout(crl::time dt);

	[[nodiscard]] auto shadowVisibilityChanged() const
		-> rpl::producer<ShadowVisibility>;

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

private:
	ScrollArea *area();

	void setOver(bool over);
	void setOverBar(bool overbar);
	void setMoving(bool moving);

	void hideTimer();

	const style::ScrollArea *_st;

	bool _vertical = true;
	bool _hiding = false;
	bool _over = false;
	bool _overbar = false;
	bool _moving = false;
	bool _topSh = false;
	bool _bottomSh = false;

	QPoint _dragStart;
	QScrollBar *_connected;

	int32 _startFrom, _scrollMax;

	crl::time _hideIn = 0;
	base::Timer _hideTimer;

	Animations::Simple _a_over;
	Animations::Simple _a_barOver;
	Animations::Simple _a_opacity;

	QRect _bar;

	rpl::event_stream<ShadowVisibility> _shadowVisibilityChanged;
};

class ScrollArea : public RpWidgetBase<QScrollArea> {
public:
	using Parent = RpWidgetBase<QScrollArea>;
	ScrollArea(QWidget *parent, const style::ScrollArea &st = st::defaultScrollArea, bool handleTouch = true);

	int scrollWidth() const;
	int scrollHeight() const;
	int scrollLeftMax() const;
	int scrollTopMax() const;
	int scrollLeft() const;
	int scrollTop() const;

	template <typename Widget>
	QPointer<Widget> setOwnedWidget(object_ptr<Widget> widget) {
		auto result = QPointer<Widget>(widget);
		doSetOwnedWidget(std::move(widget));
		return result;
	}
	template <typename Widget>
	object_ptr<Widget> takeWidget() {
		return object_ptr<Widget>::fromRaw(
			static_cast<Widget*>(doTakeWidget().release()));
	}

	void rangeChanged(int oldMax, int newMax, bool vertical);

	void updateBars();

	bool focusNextPrevChild(bool next) override;
	void setMovingByScrollBar(bool movingByScrollBar);

	bool viewportEvent(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	auto scrollTopValue() const {
		return _scrollTopUpdated.events_starting_with(scrollTop());
	}
	auto scrollTopChanges() const {
		return _scrollTopUpdated.events();
	}

	void scrollTo(ScrollToRequest request);
	void scrollToWidget(not_null<QWidget*> widget);

	void scrollToY(int toTop, int toBottom = -1);
	void disableScroll(bool dis);
	void scrolled();
	void innerResized();

	void setCustomWheelProcess(Fn<bool(not_null<QWheelEvent*>)> process) {
		_customWheelProcess = std::move(process);
	}
	void setCustomTouchProcess(Fn<bool(not_null<QTouchEvent*>)> process) {
		_customTouchProcess = std::move(process);
	}

	[[nodiscard]] rpl::producer<> scrolls() const;
	[[nodiscard]] rpl::producer<> innerResizes() const;
	[[nodiscard]] rpl::producer<> geometryChanged() const;

protected:
	bool eventHook(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

	void resizeEvent(QResizeEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void touchEvent(QTouchEvent *e);

	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

protected:
	void scrollContentsBy(int dx, int dy) override;

private:
	void doSetOwnedWidget(object_ptr<QWidget> widget);
	object_ptr<QWidget> doTakeWidget();

	bool filterOutTouchEvent(QEvent *e);
	void touchScrollTimer();
	bool touchScroll(const QPoint &delta);
	void touchScrollUpdated(const QPoint &screenPos);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	bool _disabled = false;
	bool _movingByScrollBar = false;

	const style::ScrollArea &_st;
	object_ptr<ScrollBar> _horizontalBar, _verticalBar;
	object_ptr<ScrollShadow> _topShadow, _bottomShadow;
	int _horizontalValue, _verticalValue;

	bool _touchEnabled;
	base::Timer _touchTimer;
	bool _touchScroll = false;
	bool _touchPress = false;
	bool _touchRightButton = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;

	TouchScrollState _touchScrollState = TouchScrollState::Manual;
	bool _touchPrevPosValid = false;
	bool _touchWaitingAcceleration = false;
	QPoint _touchSpeed;
	crl::time _touchSpeedTime = 0;
	crl::time _touchAccelerationTime = 0;
	crl::time _touchTime = 0;
	base::Timer _touchScrollTimer;

	Fn<bool(not_null<QWheelEvent*>)> _customWheelProcess;
	Fn<bool(not_null<QTouchEvent*>)> _customTouchProcess;
	bool _widgetAcceptsTouch = false;

	object_ptr<QWidget> _widget = { nullptr };

	rpl::event_stream<int> _scrollTopUpdated;
	rpl::event_stream<> _scrolls;
	rpl::event_stream<> _innerResizes;
	rpl::event_stream<> _geometryChanged;

};

} // namespace Ui
