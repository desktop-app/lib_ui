// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_utility_mac.h"

#include "ui/integration.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>

#include <Cocoa/Cocoa.h>

#ifndef OS_MAC_STORE
extern "C" {
void _dispatch_main_queue_callback_4CF(mach_msg_header_t *msg);
} // extern "C"
#endif // OS_MAC_STORE

namespace Ui {
namespace Platform {

bool IsApplicationActive() {
	return [[NSApplication sharedApplication] isActive];
}

void InitOnTopPanel(not_null<QWidget*> panel) {
	Expects(!panel->windowHandle());

	// Force creating windowHandle() without creating the platform window yet.
	panel->setAttribute(Qt::WA_NativeWindow, true);
	panel->windowHandle()->setProperty("_td_macNonactivatingPanelMask", QVariant(true));
	panel->setAttribute(Qt::WA_NativeWindow, false);

	panel->createWinId();

	auto platformWindow = [reinterpret_cast<NSView*>(panel->winId()) window];
	Assert([platformWindow isKindOfClass:[NSPanel class]]);

	auto platformPanel = static_cast<NSPanel*>(platformWindow);
	[platformPanel setBackgroundColor:[NSColor clearColor]];
	[platformPanel setLevel:NSModalPanelWindowLevel];
	[platformPanel setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorIgnoresCycle];
	[platformPanel setHidesOnDeactivate:NO];
	//[platformPanel setFloatingPanel:YES];

	Integration::Instance().activationFromTopPanel();
}

void DeInitOnTopPanel(not_null<QWidget*> panel) {
	auto platformWindow = [reinterpret_cast<NSView*>(panel->winId()) window];
	Assert([platformWindow isKindOfClass:[NSPanel class]]);

	auto platformPanel = static_cast<NSPanel*>(platformWindow);
	auto newBehavior = ([platformPanel collectionBehavior] & (~NSWindowCollectionBehaviorCanJoinAllSpaces)) | NSWindowCollectionBehaviorMoveToActiveSpace;
	[platformPanel setCollectionBehavior:newBehavior];
}

void ReInitOnTopPanel(not_null<QWidget*> panel) {
	auto platformWindow = [reinterpret_cast<NSView*>(panel->winId()) window];
	Assert([platformWindow isKindOfClass:[NSPanel class]]);

	auto platformPanel = static_cast<NSPanel*>(platformWindow);
	auto newBehavior = ([platformPanel collectionBehavior] & (~NSWindowCollectionBehaviorMoveToActiveSpace)) | NSWindowCollectionBehaviorCanJoinAllSpaces;
	[platformPanel setCollectionBehavior:newBehavior];
}

void ShowOverAll(not_null<QWidget*> widget, bool canFocus) {
	NSWindow *wnd = [reinterpret_cast<NSView*>(widget->winId()) window];
	[wnd setLevel:NSPopUpMenuWindowLevel];
	if (!canFocus) {
		[wnd setStyleMask:NSWindowStyleMaskUtilityWindow | NSWindowStyleMaskNonactivatingPanel];
		[wnd setCollectionBehavior:NSWindowCollectionBehaviorMoveToActiveSpace|NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorIgnoresCycle];
	}
}

void AcceptAllMouseInput(not_null<QWidget*> widget) {
	// https://github.com/telegramdesktop/tdesktop/issues/27025
	//
	// By default system clicks through fully transparent pixels,
	// and starting with macOS 14.1 it counts the transparency
	// incorrectly (as if `y` is mirrored), so when clicking
	// on a reactions strip outside of the menu column the click
	// is ignored and made on the underlying window, because at the
	// bottom of the menu in the same place there is nothing, empty.
	//
	// We explicitly request all the input to disable this behavior.
	//
	// See https://stackoverflow.com/a/29451199 and comments.
	NSWindow *window = [reinterpret_cast<NSView*>(widget->winId()) window];
	[window setIgnoresMouseEvents:NO];
}

void DrainMainQueue() {
#ifndef OS_MAC_STORE
	_dispatch_main_queue_callback_4CF(nullptr);
#endif // OS_MAC_STORE
}

void IgnoreAllActivation(not_null<QWidget*> widget) {
}

void DisableSystemWindowResize(not_null<QWidget*> widget, QSize ratio) {
	const auto winId = widget->winId();
	if (const auto view = reinterpret_cast<NSView*>(winId)) {
		if (const auto window = [view window]) {
			window.styleMask &= ~NSWindowStyleMaskResizable;
		}
	}
}

std::optional<bool> IsOverlapped(
		not_null<QWidget*> widget,
		const QRect &rect) {
	NSWindow *window = [reinterpret_cast<NSView*>(widget->winId()) window];
	Assert(window != nullptr);

	if (![window isOnActiveSpace]) {
		return true;
	}

	const auto nativeRect = QRect(
		widget->mapToGlobal(rect.topLeft()),
		rect.size()).toCGRect();

	CGWindowID windowId = (CGWindowID)[window windowNumber];
	const CGWindowListOption options = kCGWindowListExcludeDesktopElements
		| kCGWindowListOptionOnScreenAboveWindow;
	CFArrayRef windows = CGWindowListCopyWindowInfo(options, windowId);
	if (!windows) {
		return std::nullopt;
	}
	const auto guard = gsl::finally([&] {
		CFRelease(windows);
	});
	NSMutableArray *list = (__bridge NSMutableArray*)windows;
	for (NSDictionary *window in list) {
		NSNumber *alphaValue = [window objectForKey:@"kCGWindowAlpha"];
		const auto alpha = alphaValue ? [alphaValue doubleValue] : 1.;
		if (alpha == 0.) {
			continue;
		}
		NSString *owner = [window objectForKey:@"kCGWindowOwnerName"];
		NSNumber *layerValue = [window objectForKey:@"kCGWindowLayer"];
		const auto layer = layerValue ? [layerValue intValue] : 0;
		if (owner && [owner isEqualToString:@"Dock"] && layer == 20) {
			// It is always full screen.
			continue;
		}
		CFDictionaryRef bounds = (__bridge CFDictionaryRef)[window objectForKey:@"kCGWindowBounds"];
		if (!bounds) {
			continue;
		}
		CGRect rect;
		if (!CGRectMakeWithDictionaryRepresentation(bounds, &rect)) {
			continue;
		} else if (CGRectIntersectsRect(rect, nativeRect)) {
			return true;
		}
	}
	return false;
}

void SetGeometryWithPossibleScreenChange(
		not_null<QWidget*> widget,
		QRect geometry) {
	widget->setGeometry(geometry);
}

} // namespace Platform
} // namespace Ui
