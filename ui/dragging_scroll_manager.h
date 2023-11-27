// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace base {
class Timer;
} // namespace base

namespace Ui {

class DraggingScrollManager final {
public:
	DraggingScrollManager();

	void checkDeltaScroll(int delta);
	void checkDeltaScroll(const QPoint &point, int top, int bottom);
	void cancel();

	[[nodiscard]] rpl::producer<int> scrolls() const;

private:
	void scrollByTimer();

	std::unique_ptr<base::Timer> _timer;
	int _delta = 0;
	rpl::event_stream<int> _scrolls;

};

} // namespace Ui
