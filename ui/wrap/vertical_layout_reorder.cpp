// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/wrap/vertical_layout_reorder.h"

#include "ui/wrap/vertical_layout.h"
#include "styles/style_basic.h"

#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>

namespace Ui {

namespace {

constexpr auto kScrollFactor = 0.05;

} // namespace

VerticalLayoutReorder::VerticalLayoutReorder(
	not_null<VerticalLayout*> layout,
	not_null<ScrollArea*> scroll)
: _layout(layout)
, _scroll(scroll)
, _scrollAnimation([=] { updateScrollCallback(); }) {
}

VerticalLayoutReorder::VerticalLayoutReorder(not_null<VerticalLayout*> layout)
: _layout(layout) {
}

void VerticalLayoutReorder::cancel() {
	if (_currentWidget) {
		cancelCurrent(indexOf(_currentWidget));
	}
	_lifetime.destroy();
	for (auto i = 0, count = _layout->count(); i != count; ++i) {
		_layout->setVerticalShift(i, 0);
	}
	_entries.clear();
}

void VerticalLayoutReorder::start() {
	const auto count = _layout->count();
	if (count < 2) {
		return;
	}
	for (auto i = 0; i != count; ++i) {
		const auto widget = _layout->widgetAt(i);
		widget->events(
		) | rpl::start_with_next_done([=](not_null<QEvent*> e) {
			switch (e->type()) {
			case QEvent::MouseMove:
				mouseMove(
					widget,
					static_cast<QMouseEvent*>(e.get())->globalPos());
				break;
			case QEvent::MouseButtonPress:
				mousePress(
					widget,
					static_cast<QMouseEvent*>(e.get())->button(),
					static_cast<QMouseEvent*>(e.get())->globalPos());
				break;
			case QEvent::MouseButtonRelease:
				mouseRelease(static_cast<QMouseEvent*>(e.get())->button());
				break;
			}
		}, [=] {
			cancel();
		}, _lifetime);
		_entries.push_back({ widget });
	}
}

void VerticalLayoutReorder::mouseMove(
		not_null<RpWidget*> widget,
		QPoint position) {
	if (_currentWidget != widget) {
		return;
	} else if (_currentState != State::Started) {
		checkForStart(position);
	} else {
		updateOrder(indexOf(_currentWidget), position);
	}
}

void VerticalLayoutReorder::checkForStart(QPoint position) {
	const auto shift = position.y() - _currentStart;
	const auto delta = QApplication::startDragDistance();
	if (std::abs(shift) <= delta) {
		return;
	}
	_currentWidget->raise();
	_currentState = State::Started;
	_currentStart += (shift > 0) ? delta : -delta;

	const auto index = indexOf(_currentWidget);
	_currentDesiredIndex = index;
	_updates.fire({ _currentWidget, index, index, _currentState });

	updateOrder(index, position);
}

void VerticalLayoutReorder::updateOrder(int index, QPoint position) {
	const auto shift = position.y() - _currentStart;
	auto &current = _entries[index];
	current.shiftAnimation.stop();
	current.shift = current.finalShift = shift;
	_layout->setVerticalShift(index, shift);

	checkForScrollAnimation();

	const auto count = _entries.size();
	const auto currentHeight = current.widget->height();
	const auto currentMiddle = current.widget->y() + currentHeight / 2;
	_currentDesiredIndex = index;
	if (shift > 0) {
		auto top = current.widget->y() - shift;
		for (auto next = index + 1; next != count; ++next) {
			const auto &entry = _entries[next];
			top += entry.widget->height();
			if (currentMiddle < top) {
				moveToShift(next, 0);
			} else {
				_currentDesiredIndex = next;
				moveToShift(next, -currentHeight);
			}
		}
		for (auto prev = index - 1; prev >= 0; --prev) {
			moveToShift(prev, 0);
		}
	} else {
		for (auto next = index + 1; next != count; ++next) {
			moveToShift(next, 0);
		}
		for (auto prev = index - 1; prev >= 0; --prev) {
			const auto &entry = _entries[prev];
			if (currentMiddle >= entry.widget->y() - entry.shift + currentHeight) {
				moveToShift(prev, 0);
			} else {
				_currentDesiredIndex = prev;
				moveToShift(prev, currentHeight);
			}
		}
	}
}

void VerticalLayoutReorder::mousePress(
		not_null<RpWidget*> widget,
		Qt::MouseButton button,
		QPoint position) {
	if (button != Qt::LeftButton) {
		return;
	}
	cancelCurrent();
	_currentWidget = widget;
	_currentStart = position.y();
}

void VerticalLayoutReorder::mouseRelease(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	finishReordering();
}

void VerticalLayoutReorder::cancelCurrent() {
	if (_currentWidget) {
		cancelCurrent(indexOf(_currentWidget));
	}
}

void VerticalLayoutReorder::cancelCurrent(int index) {
	Expects(_currentWidget != nullptr);

	if (_currentState == State::Started) {
		_currentState = State::Cancelled;
		_updates.fire({ _currentWidget, index, index, _currentState });
	}
	_currentWidget = nullptr;
	for (auto i = 0, count = int(_entries.size()); i != count; ++i) {
		moveToShift(i, 0);
	}
}

void VerticalLayoutReorder::finishReordering() {
	if (_scroll) {
		_scrollAnimation.stop();
	}
	finishCurrent();
}

void VerticalLayoutReorder::finishCurrent() {
	if (!_currentWidget) {
		return;
	}
	const auto index = indexOf(_currentWidget);
	if (_currentDesiredIndex == index || _currentState != State::Started) {
		cancelCurrent(index);
		return;
	}
	const auto result = _currentDesiredIndex;
	const auto widget = _currentWidget;
	_currentState = State::Cancelled;
	_currentWidget = nullptr;

	auto &current = _entries[index];
	const auto height = current.widget->height();
	if (index < result) {
		auto sum = 0;
		for (auto i = index; i != result; ++i) {
			auto &entry = _entries[i + 1];
			const auto widget = entry.widget;
			entry.deltaShift += height;
			updateShift(widget, i + 1);
			sum += widget->height();
		}
		current.finalShift -= sum;
	} else if (index > result) {
		auto sum = 0;
		for (auto i = result; i != index; ++i) {
			auto &entry = _entries[i];
			const auto widget = entry.widget;
			entry.deltaShift -= height;
			updateShift(widget, i);
			sum += widget->height();
		}
		current.finalShift += sum;
	}
	if (!(current.finalShift + current.deltaShift)) {
		current.shift = 0;
		_layout->setVerticalShift(index, 0);
	}
	base::reorder(_entries, index, result);
	_layout->reorderRows(index, _currentDesiredIndex);
	for (auto i = 0, count = int(_entries.size()); i != count; ++i) {
		moveToShift(i, 0);
	}

	_updates.fire({ widget, index, result, State::Applied });
}

void VerticalLayoutReorder::moveToShift(int index, int shift) {
	auto &entry = _entries[index];
	if (entry.finalShift + entry.deltaShift == shift) {
		return;
	}
	const auto widget = entry.widget;
	entry.shiftAnimation.start(
		[=] { updateShift(widget, index); },
		entry.finalShift,
		shift - entry.deltaShift,
		st::slideWrapDuration);
	entry.finalShift = shift - entry.deltaShift;
}

void VerticalLayoutReorder::updateShift(
		not_null<RpWidget*> widget,
		int indexHint) {
	Expects(indexHint >= 0 && indexHint < _entries.size());

	const auto index = (_entries[indexHint].widget == widget)
		? indexHint
		: indexOf(widget);
	auto &entry = _entries[index];
	entry.shift = base::SafeRound(
		entry.shiftAnimation.value(entry.finalShift)
	) + entry.deltaShift;
	if (entry.deltaShift && !entry.shiftAnimation.animating()) {
		entry.finalShift += entry.deltaShift;
		entry.deltaShift = 0;
	}
	_layout->setVerticalShift(index, entry.shift);
}

int VerticalLayoutReorder::indexOf(not_null<RpWidget*> widget) const {
	const auto i = ranges::find(_entries, widget, &Entry::widget);
	Assert(i != end(_entries));
	return i - begin(_entries);
}

auto VerticalLayoutReorder::updates() const -> rpl::producer<Single> {
	return _updates.events();
}

void VerticalLayoutReorder::updateScrollCallback() {
	if (!_scroll) {
		return;
	}
	const auto delta = deltaFromEdge();
	const auto oldTop = _scroll->scrollTop();
	_scroll->scrollToY(oldTop + delta);
	const auto newTop = _scroll->scrollTop();

	_currentStart += oldTop - newTop;
	if (newTop == 0 || newTop == _scroll->scrollTopMax()) {
		_scrollAnimation.stop();
	}
}

void VerticalLayoutReorder::checkForScrollAnimation() {
	if (!_scroll || !deltaFromEdge() || _scrollAnimation.animating()) {
		return;
	}
	_scrollAnimation.start();
}

int VerticalLayoutReorder::deltaFromEdge() {
	Expects(_currentWidget != nullptr);
	Expects(_scroll);

	const auto globalPosition = _currentWidget->mapToGlobal(QPoint(0, 0));
	const auto localTop = _scroll->mapFromGlobal(globalPosition).y();
	const auto localBottom = localTop
		+ _currentWidget->height()
		- _scroll->height();

	const auto isTopEdge = (localTop < 0);
	const auto isBottomEdge = (localBottom > 0);
	if (!isTopEdge && !isBottomEdge) {
		_scrollAnimation.stop();
		return 0;
	}
	return int((isBottomEdge ? localBottom : localTop) * kScrollFactor);
}

} // namespace Ui
