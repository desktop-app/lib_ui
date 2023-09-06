// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/scroll_area.h" // For helpers, like ScrollToRequest.
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"
#include "base/timer.h"

namespace style {
struct ScrollArea;
} // namespace style

namespace st {
extern const style::ScrollArea &defaultScrollArea;
} // namespace st

namespace Ui {

struct ScrollState {
	int visibleFrom = 0;
	int visibleTill = 0;
	int fullSize = 0;

	friend inline constexpr auto operator<=>(
		const ScrollState &,
		const ScrollState &) = default;
	friend inline constexpr bool operator==(
		const ScrollState &,
		const ScrollState &) = default;
};

class ElasticScrollBar final : public RpWidget {
public:
	ElasticScrollBar(
		QWidget *parent,
		const style::ScrollArea &st,
		Qt::Orientation orientation = Qt::Vertical);

	void updateState(ScrollState state);
	void toggle(bool shown, anim::type animated = anim::type::normal);

	[[nodiscard]] rpl::producer<int> visibleFromDragged() const;

private:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	bool eventHook(QEvent *e) override;

	[[nodiscard]] int scaleToBar(int change) const;
	[[nodiscard]] bool barHighlighted() const;
	void toggleOver(bool over, anim::type animated = anim::type::normal);
	void toggleOverBar(bool over, anim::type animated = anim::type::normal);
	void toggleDragging(
		bool dragging,
		anim::type animated = anim::type::normal);
	void startBarHighlightAnimation(bool wasHighlighted);
	void refreshGeometry();

	const style::ScrollArea &_st;
	Ui::Animations::Simple _shownAnimation;
	Ui::Animations::Simple _overAnimation;
	Ui::Animations::Simple _barHighlightAnimation;
	base::Timer _hideTimer;
	rpl::event_stream<int> _visibleFromDragged;
	int _dragOverscrollAccumulated = 0;
	QRect _area;
	QRect _bar;
	QPoint _dragPosition;
	ScrollState _state;
	bool _shown : 1 = false;
	bool _over : 1 = false;
	bool _overBar : 1 = false;
	bool _vertical : 1 = false;
	bool _dragging : 1 = false;

};

struct ElasticScrollPosition {
	int value = 0;
	int overscroll = 0;

	friend inline auto operator<=>(
		ElasticScrollPosition,
		ElasticScrollPosition) = default;
	friend inline bool operator==(
		ElasticScrollPosition,
		ElasticScrollPosition) = default;
};

enum class ElasticScrollMovement {
	None,
	Progress,
	Momentum,
	Returning,
};

class ElasticScroll final : public RpWidget {
public:
	ElasticScroll(
		QWidget *parent,
		const style::ScrollArea &st = st::defaultScrollArea,
		Qt::Orientation orientation = Qt::Vertical);

	void setHandleTouch(bool handle);
	bool viewportEvent(QEvent *e);

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

	void updateBars();

	auto scrollTopValue() const {
		return _scrollTopUpdated.events_starting_with(scrollTop());
	}
	auto scrollTopChanges() const {
		return _scrollTopUpdated.events();
	}

	void scrollTo(ScrollToRequest request);
	void scrollToWidget(not_null<QWidget*> widget);
	void scrollToY(int toTop, int toBottom = -1);
	void scrollTo(int toFrom, int toTill = -1);
	void disableScroll(bool dis);
	void innerResized();

	void setCustomWheelProcess(Fn<bool(not_null<QWheelEvent*>)> process) {
		_customWheelProcess = std::move(process);
	}
	void setCustomTouchProcess(Fn<bool(not_null<QTouchEvent*>)> process) {
		_customTouchProcess = std::move(process);
	}

	enum class OverscrollType : uchar {
		None,
		Virtual,
		Real,
	};
	void setOverscrollTypes(OverscrollType from, OverscrollType till);
	void setOverscrollDefaults(int from, int till, bool shift = false);
	void setOverscrollBg(QColor bg);

	[[nodiscard]] rpl::producer<> scrolls() const;
	[[nodiscard]] rpl::producer<> innerResizes() const;
	[[nodiscard]] rpl::producer<> geometryChanged() const;

	using Position = ElasticScrollPosition;
	[[nodiscard]] Position position() const;
	[[nodiscard]] rpl::producer<Position> positionValue() const;

	using Movement = ElasticScrollMovement;
	[[nodiscard]] Movement movement() const;
	[[nodiscard]] rpl::producer<Movement> movementValue() const;

private:
	bool eventHook(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool handleWheelEvent(not_null<QWheelEvent*> e, bool touch = false);
	void handleTouchEvent(QTouchEvent *e);

	void updateState();
	void setState(ScrollState state);
	[[nodiscard]] int willScrollTo(int position) const;
	void tryScrollTo(int position, bool synthMouseMove = true);
	void applyScrollTo(int position, bool synthMouseMove = true);
	void applyOverscroll(int overscroll);

	void doSetOwnedWidget(object_ptr<QWidget> widget);
	object_ptr<QWidget> doTakeWidget();

	bool filterOutTouchEvent(QEvent *e);
	void touchScrollTimer();
	void touchScrollUpdated();
	void sendWheelEvent(Qt::ScrollPhase phase, QPoint delta = {});

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	struct AccumulatedParts {
		int base = 0;
		int relative = 0;
	};
	[[nodiscard]] AccumulatedParts computeAccumulatedParts() const;
	[[nodiscard]] int currentOverscrollDefault() const;
	[[nodiscard]] int currentOverscrollDefaultAccumulated() const;
	void overscrollReturn();
	void overscrollReturnCancel();
	void overscrollCheckReturnFinish();
	bool overscrollFinish();
	void applyAccumulatedScroll();

	const style::ScrollArea &_st;
	std::unique_ptr<ElasticScrollBar> _bar;
	ScrollState _state;

	base::Timer _touchTimer;
	base::Timer _touchScrollTimer;
	QPoint _touchStart;
	QPoint _touchPreviousPosition;
	QPoint _touchPosition;
	QPoint _touchSpeed;
	crl::time _touchSpeedTime = 0;
	crl::time _touchAccelerationTime = 0;
	crl::time _touchTime = 0;
	crl::time _lastScroll = 0;
	TouchScrollState _touchScrollState = TouchScrollState::Manual;
	int _overscrollAccumulated = 0;
	int _ignoreMomentumFromOverscroll = 0;
	bool _touchDisabled : 1 = false;
	bool _touchScroll : 1 = false;
	bool _touchPress : 1 = false;
	bool _touchRightButton : 1 = false;
	bool _touchPreviousPositionValid : 1 = false;
	bool _touchWaitingAcceleration : 1 = false;
	bool _vertical : 1 = false;
	bool _widgetAcceptsTouch : 1 = false;
	bool _disabled : 1 = false;
	bool _dirtyState : 1 = false;
	bool _overscrollReturning : 1 = false;

	Fn<bool(not_null<QWheelEvent*>)> _customWheelProcess;
	Fn<bool(not_null<QTouchEvent*>)> _customTouchProcess;
	int _overscroll = 0;
	int _overscrollDefaultFrom = 0;
	int _overscrollDefaultTill = 0;
	OverscrollType _overscrollTypeFrom = OverscrollType::Real;
	OverscrollType _overscrollTypeTill = OverscrollType::Real;
	std::optional<QColor> _overscrollBg;
	Ui::Animations::Simple _overscrollReturnAnimation;
	rpl::variable<Position> _position;
	rpl::variable<Movement> _movement;

	object_ptr<QWidget> _widget = { nullptr };

	rpl::event_stream<int> _scrollTopUpdated;
	rpl::event_stream<> _scrolls;
	rpl::event_stream<> _innerResizes;
	rpl::event_stream<> _geometryChanged;

};

[[nodiscard]] QPoint ScrollDelta(not_null<QWheelEvent*> e);

} // namespace Ui
