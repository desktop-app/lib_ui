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
#include "styles/style_widgets.h"

#include <QtWidgets/QScrollArea>
#include <QtCore/QTimer>
#include <QtGui/QtEvents>

namespace Ui {

// 37px per 15ms while select-by-drag.
inline constexpr auto kMaxScrollSpeed = 37;

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

class ScrollShadow : public QWidget {
	Q_OBJECT

public:
	ScrollShadow(ScrollArea *parent, const style::ScrollArea *st);

	void paintEvent(QPaintEvent *e);

public Q_SLOTS:
	void changeVisibility(bool shown);

private:
	const style::ScrollArea *_st;

};

class ScrollBar : public TWidget {
	Q_OBJECT

public:
	ScrollBar(ScrollArea *parent, bool vertical, const style::ScrollArea *st);

	void recountSize();
	void updateBar(bool force = false);

	void hideTimeout(crl::time dt);

private Q_SLOTS:
	void onValueChanged();
	void onRangeChanged();
	void onHideTimer();

Q_SIGNALS:
	void topShadowVisibility(bool);
	void bottomShadowVisibility(bool);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	ScrollArea *area();

	void setOver(bool over);
	void setOverBar(bool overbar);
	void setMoving(bool moving);

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
	QTimer _hideTimer;

	Animations::Simple _a_over;
	Animations::Simple _a_barOver;
	Animations::Simple _a_opacity;

	QRect _bar;
};

class ScrollArea : public RpWidgetWrap<QScrollArea> {
	Q_OBJECT

public:
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

protected:
	bool eventFilter(QObject *obj, QEvent *e) override;

	void resizeEvent(QResizeEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void touchEvent(QTouchEvent *e);

	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

public Q_SLOTS:
	void scrollToY(int toTop, int toBottom = -1);
	void disableScroll(bool dis);
	void onScrolled();
	void onInnerResized();

	void onTouchTimer();
	void onTouchScrollTimer();

Q_SIGNALS:
	void scrolled();
	void innerResized();
	void scrollStarted();
	void scrollFinished();
	void geometryChanged();

protected:
	void scrollContentsBy(int dx, int dy) override;

private:
	void doSetOwnedWidget(object_ptr<TWidget> widget);
	object_ptr<TWidget> doTakeWidget();

	void setWidget(QWidget *widget);

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
	QTimer _touchTimer;
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
	QTimer _touchScrollTimer;

	bool _widgetAcceptsTouch = false;

	object_ptr<TWidget> _widget = { nullptr };

	rpl::event_stream<int> _scrollTopUpdated;

};

} // namespace Ui
