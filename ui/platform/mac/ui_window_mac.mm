// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_window_mac.h"

#include "ui/platform/mac/ui_window_title_mac.h"
#include "base/platform/base_platform_info.h"
#include "styles/palette.h"

#include <QtCore/QCoreApplication>
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

} // namespace

class WindowHelper::Private final {
public:
	explicit Private(not_null<WindowHelper*> owner);

	[[nodiscard]] int customTitleHeight() const;

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

int WindowHelper::Private::customTitleHeight() const {
	return _customTitleHeight;
}

Fn<void(bool)> WindowHelper::Private::toggleCustomTitleCallback() {
	return [=](bool visible) {
		_owner->toggleCustomTitle(visible);
	};
}

Fn<void()> WindowHelper::Private::enforceStyleCallback() {
	return [=] {
		if (_nativeWindow && _customTitleHeight > 0) {
			[_nativeWindow setStyleMask:[_nativeWindow styleMask] | NSFullSizeContentViewWindowMask];
		}
	};
}

void WindowHelper::Private::initOpenGL() {
	auto forceOpenGL = std::make_unique<QOpenGLWidget>(_owner->_window);
}

void WindowHelper::Private::resolveWeakPointers() {
	_owner->_window->createWinId();

	_nativeView = reinterpret_cast<NSView*>(_owner->_window->winId());
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
: _window(window)
, _private(std::make_unique<Private>(this))
, _title(_private->customTitleHeight()
	? Ui::CreateChild<TitleWidget>(
		_window.get(),
		_private->customTitleHeight())
	: nullptr)
, _body(Ui::CreateChild<RpWidget>(_window.get())) {
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
	_window->setWindowTitle(
		(!_title || _title->isHidden()) ? title : QString());
}

void WindowHelper::setTitleStyle(const style::WindowTitle &st) {
	if (_title) {
		_title->setStyle(st);
	}
}

void WindowHelper::toggleCustomTitle(bool visible) {
	if (!_title || _title->isHidden() != visible) {
		return;
	}
	_title->setVisible(visible);
	_window->setWindowTitle(visible ? QString() : _title->text());
}

void WindowHelper::setMinimumSize(QSize size) {
	_window->setMinimumSize(
		size.width(),
		(_title ? _title->height() : 0) + size.height());
}

void WindowHelper::setFixedSize(QSize size) {
	_window->setFixedSize(
		size.width(),
		(_title ? _title->height() : 0) + size.height());
}

void WindowHelper::setGeometry(QRect rect) {
	_window->setGeometry(
		rect.marginsAdded({ 0, (_title ? _title->height() : 0), 0, 0 }));
}

void WindowHelper::init() {
	rpl::combine(
		_window->sizeValue(),
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

std::unique_ptr<BasicWindowHelper> CreateWindowHelper(
		not_null<RpWidget*> window) {
	return std::make_unique<WindowHelper>(window);
}

} // namespace Platform
} // namespace Ui
