// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_native_scroll_mac.h"

#import <AppKit/AppKit.h>

namespace Ui::Platform {
namespace {

constexpr auto kEntriesCount = 16;
constexpr auto kTimestampSlack = qint64(2);
constexpr auto kDeltaSlack = 1.5;

struct Entry {
	qint64 timestamp = 0;
	QPointF delta;
};

Entry Entries[kEntriesCount]/* = {}*/;
int NextEntry/* = 0*/;

// Both Qt and this monitor observe the same NSEvent on the main thread,
// so entries are recorded strictly before the QWheelEvent they produce
// is delivered.
void EnsureMonitorInstalled() {
	[[maybe_unused]] static const auto Installed = [NSEvent
		addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
		handler:^NSEvent*(NSEvent *event) {
			if (event.hasPreciseScrollingDeltas) {
				Entries[NextEntry] = {
					.timestamp = qint64(event.timestamp * 1000.),
					.delta = QPointF(
						event.scrollingDeltaX,
						event.scrollingDeltaY),
				};
				NextEntry = (NextEntry + 1) % kEntriesCount;
			}
			return event;
		}];
}

} // namespace

std::optional<QPointF> LookupNativeScrollDelta(
		not_null<const QWheelEvent*> e) {
	EnsureMonitorInstalled();
	const auto timestamp = qint64(e->timestamp());
	const auto rounded = e->pixelDelta();
	for (const auto &entry : Entries) {
		// The timestamp match is validated against the rounded delta Qt
		// produced from the same NSEvent, so a nearby unrelated event
		// can't be mistaken for this one.
		if (entry.timestamp
			&& std::abs(entry.timestamp - timestamp) <= kTimestampSlack
			&& std::abs(entry.delta.x() - rounded.x()) < kDeltaSlack
			&& std::abs(entry.delta.y() - rounded.y()) < kDeltaSlack) {
			return entry.delta;
		}
	}
	return std::nullopt;
}

} // namespace Ui::Platform
