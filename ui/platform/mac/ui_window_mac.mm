// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_window_mac.h"

#include "ui/platform/mac/ui_window_title_mac.h"
#include "ui/widgets/window.h"
#include "base/platform/base_platform_info.h"
#include "styles/palette.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QAbstractNativeEventFilter>
#include <QtGui/QWindow>
#include <QtGui/QtEvents>
#include <QtWidgets/QOpenGLWidget>
#include <Cocoa/Cocoa.h>

@interface WindowObserver : NSObject {
}

- (id) initWithToggle:(Fn<void(bool)>)toggleCustomTitleVisibility enforce:(Fn<void()>)enforceCorrectStyle;
- (void) windowWillEnterFullScreen:(NSNotification *)aNotification;
- (void) windowWillExitFullScreen:(NSNotification *)aNotification;
- (void) windowDidExitFullScreen:(NSNotification *)aNotification;

@end // @interface WindowObserver

@implementation WindowObserver {
	Fn<void(bool)> _toggleCustomTitleVisibility;
	Fn<void()> _enforceCorrectStyle;
}

- (id) initWithToggle:(Fn<void(bool)>)toggleCustomTitleVisibility enforce:(Fn<void()>)enforceCorrectStyle {
	if (self = [super init]) {
		_toggleCustomTitleVisibility = toggleCustomTitleVisibility;
		_enforceCorrectStyle = enforceCorrectStyle;
	}
	return self;
}

- (void) windowWillEnterFullScreen:(NSNotification *)aNotification {
	_toggleCustomTitleVisibility(false);
}

- (void) windowWillExitFullScreen:(NSNotification *)aNotification {
	_enforceCorrectStyle();
	_toggleCustomTitleVisibility(true);
}

- (void) windowDidExitFullScreen:(NSNotification *)aNotification {
	_enforceCorrectStyle();
}

@end // @implementation MainWindowObserver

namespace Ui {
namespace Platform {
namespace {

class LayerCreationChecker : public QObject {
public:
	LayerCreationChecker(NSView * __weak view, Fn<void()> callback)
	: _weakView(view)
	, _callback(std::move(callback)) {
		QCoreApplication::instance()->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		if (!_weakView || [_weakView layer] != nullptr) {
			_callback();
		}
		return QObject::eventFilter(object, event);
	}

private:
	NSView * __weak _weakView = nil;
	Fn<void()> _callback;

};

class EventFilter : public QObject, public QAbstractNativeEventFilter {
public:
	EventFilter(not_null<QObject*> parent, Fn<bool(void*)> checkPerformDrag)
	: QObject(parent)
	, _checkPerformDrag(std::move(checkPerformDrag)) {
		Expects(_checkPerformDrag != nullptr);
	}

	bool nativeEventFilter(
			const QByteArray &eventType,
			void *message,
			long *result) {
		NSEvent *e = static_cast<NSEvent*>(message);
		return (e && [e type] == NSEventTypeLeftMouseDown)
			? _checkPerformDrag([e window])
			: false;
		return false;
	}

private:
	Fn<bool(void*)> _checkPerformDrag;

};

} // namespace

class WindowHelper::Private final {
public:
	explicit Private(not_null<WindowHelper*> owner);
	~Private();

	[[nodiscard]] int customTitleHeight() const;
	[[nodiscard]] QRect controlsRect() const;
	[[nodiscard]] bool checkNativeMove(void *nswindow) const;
	void activateBeforeNativeMove();
	void close();

private:
	void init();
	void initOpenGL();
	void resolveWeakPointers();
	void initCustomTitle();

	[[nodiscard]] Fn<void(bool)> toggleCustomTitleCallback();
	[[nodiscard]] Fn<void()> enforceStyleCallback();

	const not_null<WindowHelper*> _owner;
	const WindowObserver *_observer = nullptr;

	NSWindow * __weak _nativeWindow = nil;
	NSView * __weak _nativeView = nil;

	std::unique_ptr<LayerCreationChecker> _layerCreationChecker;

	int _customTitleHeight = 0;

};

WindowHelper::Private::Private(not_null<WindowHelper*> owner)
: _owner(owner)
, _observer([[WindowObserver alloc] initWithToggle:toggleCustomTitleCallback() enforce:enforceStyleCallback()]) {
	init();
}

WindowHelper::Private::~Private() {
	[_observer release];
}

int WindowHelper::Private::customTitleHeight() const {
	return _customTitleHeight;
}

QRect WindowHelper::Private::controlsRect() const {
	const auto button = [&](NSWindowButton type) {
		auto view = [_nativeWindow standardWindowButton:type];
		if (!view) {
			return QRect();
		}
		auto result = [view frame];
		for (auto parent = [view superview]; parent != nil; parent = [parent superview]) {
			const auto origin = [parent frame].origin;
			result.origin.x += origin.x;
			result.origin.y += origin.y;
		}
		return QRect(result.origin.x, result.origin.y, result.size.width, result.size.height);
	};
	auto result = QRect();
	const auto buttons = {
		NSWindowCloseButton,
		NSWindowMiniaturizeButton,
		NSWindowZoomButton,
	};
	for (const auto type : buttons) {
		result = result.united(button(type));
	}
	return QRect(
		result.x(),
		[_nativeWindow frame].size.height - result.y() - result.height(),
		result.width(),
		result.height());
}

bool WindowHelper::Private::checkNativeMove(void *nswindow) const {
	if (_nativeWindow != nswindow
		|| ([_nativeWindow styleMask] & NSFullScreenWindowMask) == NSFullScreenWindowMask) {
		return false;
	}
	const auto cgReal = [NSEvent mouseLocation];
	const auto real = QPointF(cgReal.x, cgReal.y);
	const auto cgFrame = [_nativeWindow frame];
	const auto frame = QRectF(cgFrame.origin.x, cgFrame.origin.y, cgFrame.size.width, cgFrame.size.height);
	const auto border = QMarginsF{ 3., 3., 3., 3. };
	return frame.marginsRemoved(border).contains(real);
}

void WindowHelper::Private::activateBeforeNativeMove() {
	[_nativeWindow makeKeyAndOrderFront:_nativeWindow];
}

void WindowHelper::Private::close() {
	[_nativeWindow close];
}

Fn<void(bool)> WindowHelper::Private::toggleCustomTitleCallback() {
	return crl::guard(_owner->window(), [=](bool visible) {
		_owner->_titleVisible = visible;
		_owner->updateCustomTitleVisibility(true);
	});
}

Fn<void()> WindowHelper::Private::enforceStyleCallback() {
	return crl::guard(_owner->window(), [=] {
		if (_nativeWindow && _customTitleHeight > 0) {
			[_nativeWindow setStyleMask:[_nativeWindow styleMask] | NSFullSizeContentViewWindowMask];
		}
	});
}

void WindowHelper::Private::initOpenGL() {
	auto forceOpenGL = std::make_unique<QOpenGLWidget>(_owner->window());
}

void WindowHelper::Private::resolveWeakPointers() {
	_owner->window()->createWinId();

	_nativeView = reinterpret_cast<NSView*>(_owner->window()->winId());
	_nativeWindow = _nativeView ? [_nativeView window] : nullptr;

	Ensures(_nativeWindow != nullptr);
}

void WindowHelper::Private::initCustomTitle() {
	if (![_nativeWindow respondsToSelector:@selector(contentLayoutRect)]
		|| ![_nativeWindow respondsToSelector:@selector(setTitlebarAppearsTransparent:)]) {
		return;
	}

	[_nativeWindow setTitlebarAppearsTransparent:YES];

	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowWillEnterFullScreen:) name:NSWindowWillEnterFullScreenNotification object:_nativeWindow];
	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowWillExitFullScreen:) name:NSWindowWillExitFullScreenNotification object:_nativeWindow];
	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowDidExitFullScreen:) name:NSWindowDidExitFullScreenNotification object:_nativeWindow];

	// Qt has bug with layer-backed widgets containing QOpenGLWidgets.
	// See https://bugreports.qt.io/browse/QTBUG-64494
	// Emulate custom title instead (code below).
	//
	// Tried to backport a fix, testing.
	[_nativeWindow setStyleMask:[_nativeWindow styleMask] | NSFullSizeContentViewWindowMask];
	auto inner = [_nativeWindow contentLayoutRect];
	auto full = [_nativeView frame];
	_customTitleHeight = qMax(qRound(full.size.height - inner.size.height), 0);

	// Qt still has some bug with layer-backed widgets containing QOpenGLWidgets.
	// See https://github.com/telegramdesktop/tdesktop/issues/4150
	// Tried to workaround it by catching the first moment we have CALayer created
	// and explicitly setting contentsScale to window->backingScaleFactor there.
	_layerCreationChecker = std::make_unique<LayerCreationChecker>(_nativeView, [=] {
		if (_nativeView && _nativeWindow) {
			if (CALayer *layer = [_nativeView layer]) {
				[layer setContentsScale: [_nativeWindow backingScaleFactor]];
				_layerCreationChecker = nullptr;
			}
		} else {
			_layerCreationChecker = nullptr;
		}
	});
}

void WindowHelper::Private::init() {
	initOpenGL();
	resolveWeakPointers();
	initCustomTitle();
}

WindowHelper::WindowHelper(not_null<RpWidget*> window)
: BasicWindowHelper(window)
, _private(std::make_unique<Private>(this))
, _title(_private->customTitleHeight()
	? Ui::CreateChild<TitleWidget>(
		window.get(),
		_private->customTitleHeight())
	: nullptr)
, _body(Ui::CreateChild<RpWidget>(window.get())) {
	if (_title->shouldBeHidden()) {
		updateCustomTitleVisibility();
	}
	init();
}

WindowHelper::~WindowHelper() {
}

not_null<RpWidget*> WindowHelper::body() {
	return _body;
}

void WindowHelper::setTitle(const QString &title) {
	if (_title) {
		_title->setText(title);
	}
	window()->setWindowTitle(
		(!_title || !_titleVisible) ? title : QString());
}

void WindowHelper::setTitleStyle(const style::WindowTitle &st) {
	if (_title) {
		_title->setStyle(st);
		if (_title->shouldBeHidden()) {
			updateCustomTitleVisibility();
		}
	}
}

void WindowHelper::updateCustomTitleVisibility(bool force) {
	auto visible = !_title->shouldBeHidden() && _titleVisible;
	if (!_title || (!force && _title->isHidden() != visible)) {
		return;
	}
	_title->setVisible(visible);
	window()->setWindowTitle(_titleVisible ? QString() : _title->text());
}

void WindowHelper::setMinimumSize(QSize size) {
	window()->setMinimumSize(
		size.width(),
		(_title ? _title->height() : 0) + size.height());
}

void WindowHelper::setFixedSize(QSize size) {
	window()->setFixedSize(
		size.width(),
		(_title ? _title->height() : 0) + size.height());
}

void WindowHelper::setGeometry(QRect rect) {
	window()->setGeometry(
		rect.marginsAdded({ 0, (_title ? _title->height() : 0), 0, 0 }));
}

void WindowHelper::setupBodyTitleAreaEvents() {
#ifndef OS_OSX
	const auto controls = _private->controlsRect();
	qApp->installNativeEventFilter(new EventFilter(window(), [=](void *nswindow) {
		const auto point = body()->mapFromGlobal(QCursor::pos());
		if (_private->checkNativeMove(nswindow)
			&& !controls.contains(point)
			&& (bodyTitleAreaHit(point) & WindowTitleHitTestFlag::Move)) {
			_private->activateBeforeNativeMove();
			window()->windowHandle()->startSystemMove();
			return true;
		}
		return false;
	}));
#else // OS_OSX
	// OS X 10.10 doesn't have performWindowDragWithEvent yet.
	body()->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto hitTest = [&] {
			return bodyTitleAreaHit(
				static_cast<QMouseEvent*>(e.get())->pos());
		};
		if (e->type() == QEvent::MouseButtonRelease
			&& (static_cast<QMouseEvent*>(e.get())->button()
				== Qt::LeftButton)) {
			_drag = std::nullopt;
		} else if (e->type() == QEvent::MouseButtonPress
			&& hitTest()
			&& (static_cast<QMouseEvent*>(e.get())->button()
				== Qt::LeftButton)) {
			_drag = { window()->pos(), static_cast<QMouseEvent*>(e.get())->globalPos() };
		} else if (e->type() == QEvent::MouseMove && _drag && !window()->isFullScreen()) {
			const auto delta = static_cast<QMouseEvent*>(e.get())->globalPos() - _drag->dragStartPosition;
			window()->move(_drag->windowStartPosition + delta);
		}
	}, body()->lifetime());
#endif // OS_OSX
}

void WindowHelper::close() {
	_private->close();
}

void WindowHelper::init() {
	rpl::combine(
		window()->sizeValue(),
		_title->heightValue(),
		_title->shownValue()
	) | rpl::start_with_next([=](QSize size, int titleHeight, bool shown) {
		if (!shown) {
			titleHeight = 0;
		}
		_body->setGeometry(
			0,
			titleHeight,
			size.width(),
			size.height() - titleHeight);
	}, _body->lifetime());
}

std::unique_ptr<BasicWindowHelper> CreateSpecialWindowHelper(
		not_null<RpWidget*> window) {
	return std::make_unique<WindowHelper>(window);
}

} // namespace Platform
} // namespace Ui
