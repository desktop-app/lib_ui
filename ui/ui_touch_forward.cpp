// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/ui_touch_forward.h"

#include <QtGui/QTouchEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace Ui {
namespace {

using TouchPoint = QTouchEvent::TouchPoint;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
constexpr auto kPressed = Qt::TouchPointPressed;
constexpr auto kReleased = Qt::TouchPointReleased;
constexpr auto kStationary = Qt::TouchPointStationary;
#else // Qt < 6.0.0
constexpr auto kPressed = QEventPoint::State::Pressed;
constexpr auto kReleased = QEventPoint::State::Released;
constexpr auto kStationary = QEventPoint::State::Stationary;
#endif // Qt >= 6.0.0

[[nodiscard]] QPoint Global(const TouchPoint &point) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	return point.screenPos().toPoint();
#else // Qt < 6.0.0
	return point.globalPosition().toPoint();
#endif // Qt >= 6.0.0
}

// The kind of event a widget is given for the points it got, told apart the way
// Qt tells it apart: only what the points say together, so that a touch that
// went nowhere is not sent at all.
[[nodiscard]] std::optional<QEvent::Type> TypeOf(
		const QList<TouchPoint> &points) {
	auto states = 0;
	for (const auto &point : points) {
		states |= int(point.state());
	}
	if (states == int(kPressed)) {
		return QEvent::TouchBegin;
	} else if (states == int(kReleased)) {
		return QEvent::TouchEnd;
	} else if (states == int(kStationary)) {
		return std::nullopt;
	}
	return QEvent::TouchUpdate;
}

// QApplication::notify() offers the beginning of a touch to the widget under it
// and then to each of its parents, and marks the one that took it with this
// attribute - which is the only way from the outside to learn where the rest of
// that touch belongs, because updates and ends are never offered around.
[[nodiscard]] QWidget *TookTheTouch(QWidget *from) {
	for (auto widget = from; widget; widget = widget->parentWidget()) {
		if (widget->testAttribute(Qt::WA_WState_AcceptedTouchBeginEvent)) {
			return widget;
		} else if (widget->isWindow()) {
			break;
		}
	}
	return nullptr;
}

} // namespace

bool TouchForward::handle(
		not_null<QWidget*> window,
		not_null<QTouchEvent*> event) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	const auto &points = event->touchPoints();
#else // Qt < 6.0.0
	const auto &points = event->points();
#endif // Qt >= 6.0.0

	auto groups = std::vector<std::pair<QWidget*, QList<TouchPoint>>>();
	const auto add = [&](QWidget *widget, const TouchPoint &point) {
		for (auto &[already, list] : groups) {
			if (already == widget) {
				list.push_back(point);
				return;
			}
		}
		groups.push_back({ widget, QList<TouchPoint>{ point } });
	};
	for (const auto &point : points) {
		if (point.state() == kPressed) {
			const auto child = window->childAt(
				window->mapFromGlobal(Global(point)));
			add(child ? child : window.get(), point);
		} else if (const auto i = _taken.find(point.id());
				i != _taken.end()) {
			if (const auto widget = i->second.data()) {
				add(widget, point);
			} else {
				_taken.erase(i);
			}
		}
	}

	auto accepted = false;
	for (const auto &[widget, list] : groups) {
		const auto type = TypeOf(list);
		if (!type) {
			continue;
		}
		const auto guard = QPointer<QWidget>(widget);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		auto states = Qt::TouchPointStates();
		for (const auto &point : list) {
			states |= point.state();
		}
		auto forwarded = QTouchEvent(
			*type,
			const_cast<QTouchDevice*>(event->device()),
			event->modifiers(),
			states,
			list);
#else // Qt < 6.0.0
		auto forwarded = QTouchEvent(
			*type,
			event->pointingDevice(),
			event->modifiers(),
			list);
#endif // Qt >= 6.0.0
		forwarded.setTimestamp(event->timestamp());
		if (QApplication::sendEvent(widget, &forwarded)
			&& forwarded.isAccepted()) {
			accepted = true;
		}
		if (*type == QEvent::TouchBegin) {
			if (const auto took = TookTheTouch(guard.data())) {
				for (const auto &point : list) {
					_taken[point.id()] = took;
				}
			}
		}
	}

	for (const auto &point : points) {
		if (point.state() == kReleased) {
			if (const auto i = _taken.find(point.id()); i != _taken.end()) {
				_taken.erase(i);
			}
		}
	}
	return accepted;
}

} // namespace Ui
