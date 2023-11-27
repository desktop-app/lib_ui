// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/dragging_scroll_manager.h"

#include "base/timer.h"

namespace Ui {
namespace {

// 37px per 15ms while select-by-drag.
inline constexpr auto kMaxScrollSpeed = 37;

} // namespace

DraggingScrollManager::DraggingScrollManager() = default;

void DraggingScrollManager::scrollByTimer() {
	const auto d = (_delta > 0)
		? std::min(_delta * 3 / 20 + 1, kMaxScrollSpeed)
		: std::max(_delta * 3 / 20 - 1, -kMaxScrollSpeed);
	_scrolls.fire_copy(d);
}

void DraggingScrollManager::checkDeltaScroll(int delta) {
	_delta = delta;
	if (_delta) {
		if (!_timer) {
			_timer = std::make_unique<base::Timer>([=] { scrollByTimer(); });
		}
		_timer->callEach(15);
	} else {
		cancel();
	}
}

void DraggingScrollManager::checkDeltaScroll(
		const QPoint &point,
		int top,
		int bottom) {
	const auto diff = point.y() - top;
	checkDeltaScroll((diff < 0)
		? diff
		: (point.y() >= bottom)
		? (point.y() - bottom + 1)
		: 0);
}

void DraggingScrollManager::cancel() {
	if (_timer) {
		_timer->cancel();
		_timer = nullptr;
	}
}

rpl::producer<int> DraggingScrollManager::scrolls() const {
	return _scrolls.events();
}

} // namespace Ui
