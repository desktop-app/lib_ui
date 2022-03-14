/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/dragging_scroll_manager.h"

#include "ui/widgets/scroll_area.h"

namespace Ui {

DraggingScrollManager::DraggingScrollManager()
: _timer([=] { scrollByTimer(); }) {
}

void DraggingScrollManager::scrollByTimer() {
	const auto d = (_delta > 0)
		? std::min(_delta * 3 / 20 + 1, kMaxScrollSpeed)
		: std::max(_delta * 3 / 20 - 1, -kMaxScrollSpeed);
	_scrolls.fire_copy(d);
}

void DraggingScrollManager::checkDeltaScroll(int delta) {
	_delta = delta;
	if (_delta) {
		_timer.callEach(15);
	} else {
		_timer.cancel();
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
	_timer.cancel();
}

rpl::producer<int> DraggingScrollManager::scrolls() {
	return _scrolls.events();
}

} // namespace Ui
