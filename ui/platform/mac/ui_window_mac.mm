// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_window_mac.h"

#include "ui/platform/mac/ui_window_title_mac.h"
#include "ui/widgets/rp_window.h"
#include "base/qt_adapters.h"
#include "base/platform/base_platform_info.h"
#include "styles/palette.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QAbstractNativeEventFilter>
#include <QtGui/QWindow>
#include <QtGui/QtEvents>
#include <QOpenGLWidget>
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
	EventFilter(
		not_null<QObject*> parent,
		Fn<bool()> checkStartDrag,
		Fn<bool(void*)> checkPerformDrag)
	: QObject(parent)
	, _checkStartDrag(std::move(checkStartDrag))
	, _checkPerformDrag(std::move(checkPerformDrag)) {
		Expects(_checkPerformDrag != nullptr);
		Expects(_checkStartDrag != nullptr);
	}

	bool nativeEventFilter(
			const QByteArray &eventType,
			void *message,
			base::NativeEventResult *result) {
		if (NSEvent *e = static_cast<NSEvent*>(message)) {
			if ([e type] == NSEventTypeLeftMouseDown) {
				_dragStarted = _checkStartDrag();
			} else if (([e type] == NSEventTypeLeftMouseDragged)
					&& _dragStarted) {
				return _checkPerformDrag([e window]);
			}
		}
		return false;
	}

private:
	bool _dragStarted = false;
	Fn<bool()> _checkStartDrag;
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
	void setStaysOnTop(bool enabled);
	void close();

private:
	void init();
	void initOpenGL();
	void resolveWeakPointers();
	void initCustomTitle();

	[[nodiscard]] Fn<void(bool)> toggleCustomTitleCallback();
	[[nodiscard]] Fn<void()> enforceStyleCallback();
	void enforceStyle();

	const not_null<WindowHelper*> _owner;
	const WindowObserver *_observer = nullptr;

	NSWindow * __weak _nativeWindow = nil;
	NSView * __weak _nativeView = nil;

	std::unique_ptr<LayerCreationChecker> _layerCreationChecker;

	int _customTitleHeight = 0;

};

WindowHelper::Private::Private(not_null<WindowHelper*> owner)
: _owner(owner) {
	init();
}

WindowHelper::Private::~Private() {
	if (_observer) {
		[_observer release];
	}
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

void WindowHelper::Private::setStaysOnTop(bool enabled) {
	_owner->BasicWindowHelper::setStaysOnTop(enabled);
	resolveWeakPointers();
	initCustomTitle();
}

void WindowHelper::Private::close() {
	const auto weak = Ui::MakeWeak(_owner->window());
	QCloseEvent e;
	qApp->sendEvent(_owner->window(), &e);
	if (e.isAccepted() && weak && _nativeWindow) {
		[_nativeWindow close];
	}
}

Fn<void(bool)> WindowHelper::Private::toggleCustomTitleCallback() {
	return crl::guard(_owner->window(), [=](bool visible) {
		_owner->_titleVisible = visible;
		_owner->updateCustomTitleVisibility(true);
	});
}

Fn<void()> WindowHelper::Private::enforceStyleCallback() {
	return crl::guard(_owner->window(), [=] { enforceStyle(); });
}

void WindowHelper::Private::enforceStyle() {
	if (_nativeWindow && _customTitleHeight > 0) {
		[_nativeWindow setStyleMask:[_nativeWindow styleMask] | NSFullSizeContentViewWindowMask];
	}
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

	if (_observer) {
		[_observer release];
	}
	_observer = [[WindowObserver alloc] initWithToggle:toggleCustomTitleCallback() enforce:enforceStyleCallback()];
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
, _title(Ui::CreateChild<TitleWidget>(
	window.get(),
	_private->customTitleHeight()))
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

QMargins WindowHelper::frameMargins() {
	const auto titleHeight = !_title->isHidden() ? _title->height() : 0;
	return QMargins{ 0, titleHeight, 0, 0 };
}

void WindowHelper::setTitle(const QString &title) {
	_title->setText(title);
	window()->setWindowTitle(_titleVisible ? QString() : title);
}

void WindowHelper::setTitleStyle(const style::WindowTitle &st) {
	_title->setStyle(st);
	if (_title->shouldBeHidden()) {
		updateCustomTitleVisibility();
	}
}

void WindowHelper::updateCustomTitleVisibility(bool force) {
	const auto visible = !_title->shouldBeHidden() && _titleVisible;
	if (!force && _title->isHidden() != visible) {
		return;
	}
	_title->setVisible(visible);
	window()->setWindowTitle(_titleVisible ? QString() : _title->text());
}

void WindowHelper::setMinimumSize(QSize size) {
	window()->setMinimumSize(size.width(), _title->height() + size.height());
}

void WindowHelper::setFixedSize(QSize size) {
	window()->setFixedSize(size.width(), _title->height() + size.height());
}

void WindowHelper::setStaysOnTop(bool enabled) {
	_private->setStaysOnTop(enabled);
}

void WindowHelper::setGeometry(QRect rect) {
	window()->setGeometry(rect.marginsAdded({ 0, _title->height(), 0, 0 }));
}

void WindowHelper::setupBodyTitleAreaEvents() {
	const auto controls = _private->controlsRect();
	qApp->installNativeEventFilter(new EventFilter(window(), [=] {
		const auto point = body()->mapFromGlobal(QCursor::pos());
		return (bodyTitleAreaHit(point) & WindowTitleHitTestFlag::Move);
	}, [=](void *nswindow) {
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
}

void WindowHelper::close() {
	_private->close();
}

void WindowHelper::init() {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		Ui::ForceFullRepaint(window());
	}, window()->lifetime());

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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	setBodyTitleArea([](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		return (widgetPoint.y() < 0)
			? (Flag::Move | Flag::Maximize)
			: Flag::None;
	});
#endif // Qt >= 6.0.0
}

std::unique_ptr<BasicWindowHelper> CreateSpecialWindowHelper(
		not_null<RpWidget*> window) {
	return std::make_unique<WindowHelper>(window);
}

bool NativeWindowFrameSupported() {
	return false;
}

} // namespace Platform
} // namespace Ui
