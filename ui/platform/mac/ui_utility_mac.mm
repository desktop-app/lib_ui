// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_utility_mac.h"

#include "ui/integration.h"
#include "base/qt_adapters.h"

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

void StartTranslucentPaint(QPainter &p, const QRegion &region) {
	p.setCompositionMode(QPainter::CompositionMode_Source);
	for (const auto rect : region) {
		p.fillRect(rect, Qt::transparent);
	}
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void ShowOverAll(not_null<QWidget*> widget, bool canFocus) {
	NSWindow *wnd = [reinterpret_cast<NSView*>(widget->winId()) window];
	[wnd setLevel:NSPopUpMenuWindowLevel];
	if (!canFocus) {
		[wnd setStyleMask:NSUtilityWindowMask | NSNonactivatingPanelMask];
		[wnd setCollectionBehavior:NSWindowCollectionBehaviorMoveToActiveSpace|NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorIgnoresCycle];
	}
}

void BringToBack(not_null<QWidget*> widget) {
	NSWindow *wnd = [reinterpret_cast<NSView*>(widget->winId()) window];
	[wnd setLevel:NSModalPanelWindowLevel];
}

void DrainMainQueue() {
#ifndef OS_MAC_STORE
	_dispatch_main_queue_callback_4CF(nullptr);
#endif // OS_MAC_STORE
}

void IgnoreAllActivation(not_null<QWidget*> widget) {
}

std::optional<bool> IsOverlapped(
		not_null<QWidget*> widget,
		const QRect &rect) {
	NSWindow *window = [reinterpret_cast<NSView*>(widget->window()->winId()) window];
	Assert(window != nullptr);

	if (![window isOnActiveSpace]) {
		return true;
	}

	const auto nativeRect = CGRectMake(
		rect.x(),
		rect.y(),
		rect.width(),
		rect.height());

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

TitleControls::Layout TitleControlsLayout() {
	return TitleControls::Layout{
		.left = {
			TitleControls::Control::Close,
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
		}
	};
}

} // namespace Platform
} // namespace Ui
