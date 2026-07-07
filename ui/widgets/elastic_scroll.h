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

class ElasticScroll : public RpWidget {
public:
	ElasticScroll(
		QWidget *parent,
		const style::ScrollArea &st = st::defaultScrollArea,
		Qt::Orientation orientation = Qt::Vertical);
	~ElasticScroll();

	void setHandleTouch(bool handle);
	bool viewportEvent(QEvent *e);
	void keyPressEvent(QKeyEvent *e) override;

	QWidget *viewport() const; // Dummy.

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
	[[nodiscard]] QWidget *widget() const;

	void updateBars();

	auto scrollTopValue() const {
		return _vertical
			? _scrollValueUpdated.events_starting_with(scrollTop())
			: (rpl::single(0) | rpl::type_erased);
	}
	auto scrollTopChanges() const {
		return _vertical
			? _scrollValueUpdated.events()
			: (rpl::never<int>() | rpl::type_erased);
	}
	auto scrollLeftValue() const {
		return _vertical
			? (rpl::single(0) | rpl::type_erased)
			: _scrollValueUpdated.events_starting_with(scrollLeft());
	}
	auto scrollLeftChanges() const {
		return _vertical
			? (rpl::never<int>() | rpl::type_erased)
			: _scrollValueUpdated.events();
	}

	void scrollTo(ScrollToRequest request);
	void scrollToWidget(not_null<QWidget*> widget);
	void scrollToY(int toTop, int toBottom = -1);
	void scrollTo(int toFrom, int toTill = -1);
	[[nodiscard]] int computeScrollToY(int toTop, int toBottom = -1);
	void disableScroll(bool dis);
	void innerResized();

	void setCustomWheelProcess(Fn<bool(not_null<QWheelEvent*>)> process) {
		_customWheelProcess = std::move(process);
	}
	void setCustomTouchProcess(Fn<bool(not_null<QTouchEvent*>)> process) {
		_customTouchProcess = std::move(process);
	}

	// Receives wheel input on the axis this scroll doesn't handle:
	// without lockWheelDirection() every event where that axis dominates,
	// with it whole gestures locked to that axis.
	void setCrossAxisWheelProcess(Fn<bool(QPoint)> process) {
		_crossAxisWheelProcess = std::move(process);
	}

	// Locks each phased wheel gesture to the axis chosen at its start:
	// cross-axis gestures go whole to the cross-axis process (or are
	// discarded) and never scroll this area; NoScrollPhase (classic
	// wheel) events keep per-event routing.
	void lockWheelDirection() {
		_wheelDirectionLocked = true;
	}

	enum class OverscrollType : uchar {
		None,
		Virtual,
		Real,
	};
	void setOverscrollTypes(OverscrollType from, OverscrollType till);
	void setOverscrollDefaults(int from, int till, bool shift = false);
	void setOverscrollBg(QColor bg);
	void setContentBottomInset(int inset);

	// Decides, while scrolling heads toward an edge, whether the elastic
	// overscroll (bounce) is allowed for that edge. Predicates return true
	// when that edge is a genuine boundary (fully loaded) and the bounce
	// is wanted; a null predicate means always allowed. Predicates are
	// cheap, so they are re-evaluated on each event that would grow the
	// overscroll; a disallowed edge just drops the accumulated overscroll,
	// the same way an edge with OverscrollType::None does.
	void setOverscrollEdges(Fn<bool()> allowTop, Fn<bool()> allowBottom);

	// Called synchronously when a user scroll gesture (wheel, trackpad,
	// touch or Down/PageDown key) pushes past the bottom edge, before
	// any overscroll accumulates. The callback may synchronously append
	// content below, growing the inner widget at once, and return true;
	// the remaining delta of the same gesture then continues into the
	// new content instead of bouncing. Returning false (or a null
	// callback) keeps the normal edge behavior. Scrollbar drags and
	// programmatic scrolls never fire it.
	void setBottomContentRequest(Fn<bool()> request);

	[[nodiscard]] rpl::producer<> scrolls() const;
	[[nodiscard]] rpl::producer<> innerResizes() const;
	[[nodiscard]] rpl::producer<> geometryChanged() const;

	using Position = ElasticScrollPosition;
	[[nodiscard]] Position position() const;
	[[nodiscard]] rpl::producer<Position> positionValue() const;

	using Movement = ElasticScrollMovement;
	[[nodiscard]] Movement movement() const;
	[[nodiscard]] rpl::producer<Movement> movementValue() const;

	[[nodiscard]] rpl::producer<bool> touchMaybePressing() const;

	void setBarTopInset(int inset);
	void setBarBottomInset(int inset);

private:
	bool eventHook(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void moveEvent(QMoveEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	bool handleWheelEvent(not_null<QWheelEvent*> e, bool touch = false);
	bool handleScrollEvent(
		Qt::ScrollPhase phase,
		int delta,
		bool ignore = false,
		bool touch = false);
	bool requestBottomContent(int delta);
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
	int _barTopInset = 0;
	int _barBottomInset = 0;
	int _contentBottomInset = 0;
	ScrollState _state;

	QPointer<QScroller> _scroller;
	QPoint _wheelPos;

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
	rpl::variable<bool> _touchMaybePressing;
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
	bool _wheelDirectionLocked : 1 = false;
	bool _insideBottomContentRequest : 1 = false;

	Fn<bool(not_null<QWheelEvent*>)> _customWheelProcess;
	Fn<bool(not_null<QTouchEvent*>)> _customTouchProcess;
	Fn<bool(QPoint)> _crossAxisWheelProcess;
	ScrollDirectionLock _wheelDirectionLock;
	int _overscroll = 0;
	int _overscrollDefaultFrom = 0;
	int _overscrollDefaultTill = 0;
	OverscrollType _overscrollTypeFrom = OverscrollType::Real;
	OverscrollType _overscrollTypeTill = OverscrollType::Real;
	Fn<bool()> _overscrollAllowFrom;
	Fn<bool()> _overscrollAllowTill;
	Fn<bool()> _bottomContentRequest;
	std::optional<QColor> _overscrollBg;
	Ui::Animations::Simple _overscrollReturnAnimation;
	rpl::variable<Position> _position;
	rpl::variable<Movement> _movement;

	object_ptr<QWidget> _widget = { nullptr };

	rpl::event_stream<int> _scrollValueUpdated;
	rpl::event_stream<> _scrolls;
	rpl::event_stream<> _innerResizes;
	rpl::event_stream<> _geometryChanged;

};

[[nodiscard]] int OverscrollFromAccumulated(int accumulated);
[[nodiscard]] int OverscrollToAccumulated(int overscroll);

} // namespace Ui
