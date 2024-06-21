// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/rp_widget.h"

#include "base/qt_signal_producer.h"

#include <QtGui/QWindow>
#include <QtGui/QtEvents>
#include <QtGui/QColorSpace>

TWidget::TWidget(QWidget *parent)
: TWidgetHelper<QWidget>(parent) {
	[[maybe_unused]] static const auto Once = [] {
		auto format = QSurfaceFormat::defaultFormat();
		format.setSwapInterval(0);
#ifdef Q_OS_MAC
		format.setColorSpace(QColorSpace::SRgb);
#endif // Q_OS_MAC
		QSurfaceFormat::setDefaultFormat(format);
		return true;
	}();
}

namespace Ui {
namespace {

[[nodiscard]] std::vector<QPointer<QWidget>> GetChildWidgets(
		not_null<QWidget*> widget) {
	const auto &children = widget->children();
	auto result = std::vector<QPointer<QWidget>>();
	result.reserve(children.size());
	for (const auto child : children) {
		if (child && child->isWidgetType()) {
			result.push_back(static_cast<QWidget*>(child));
		}
	}
	return result;
}

} // namespace

void ToggleChildrenVisibility(not_null<QWidget*> widget, bool visible) {
	for (const auto &child : GetChildWidgets(widget)) {
		if (child) {
			child->setVisible(visible);
		}
	}
}

void ResizeFitChild(
		not_null<RpWidget*> parent,
		not_null<RpWidget*> child,
		int heightMin) {
	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		child->resizeToWidth(width);
	}, child->lifetime());

	child->heightValue(
	) | rpl::start_with_next([=](int height) {
		parent->resize(parent->width(), std::max(height, heightMin));
	}, child->lifetime());
}

rpl::producer<not_null<QEvent*>> RpWidgetWrap::events() const {
	auto &stream = eventStreams().events;
	return stream.events();
}

rpl::producer<QRect> RpWidgetWrap::geometryValue() const {
	auto &stream = eventStreams().geometry;
	return stream.events_starting_with_copy(rpWidget()->geometry());
}

rpl::producer<QSize> RpWidgetWrap::sizeValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.size(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::heightValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.height(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::widthValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.width(); })
		| rpl::distinct_until_changed();
}

rpl::producer<QPoint> RpWidgetWrap::positionValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.topLeft(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::leftValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.left(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::topValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.top(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::desiredHeightValue() const {
	return heightValue();
}

rpl::producer<bool> RpWidgetWrap::shownValue() const {
	auto &stream = eventStreams().shown;
	return stream.events_starting_with(!rpWidget()->isHidden())
		| rpl::distinct_until_changed();
}

rpl::producer<not_null<QScreen*>> RpWidgetWrap::screenValue() const {
	auto &stream = eventStreams().screen;
	return stream.events_starting_with(rpWidget()->screen());
}

rpl::producer<bool> RpWidgetWrap::windowActiveValue() const {
	auto &stream = eventStreams().windowActive;
	return stream.events_starting_with(rpWidget()->isActiveWindow());
}

rpl::producer<QRect> RpWidgetWrap::paintRequest() const {
	return eventStreams().paint.events();
}

rpl::producer<> RpWidgetWrap::alive() const {
	return eventStreams().alive.events();
}

rpl::producer<> RpWidgetWrap::macWindowDeactivateEvents() const {
#ifdef Q_OS_MAC
	return windowActiveValue()
		| rpl::skip(1)
		| rpl::filter(!rpl::mappers::_1)
		| rpl::to_empty;
#else // Q_OS_MAC
	return rpl::never<rpl::empty_value>();
#endif // Q_OS_MAC
}

rpl::lifetime &RpWidgetWrap::lifetime() {
	return _lifetime;
}

bool RpWidgetWrap::handleEvent(QEvent *event) {
	Expects(event != nullptr);

	auto streams = _eventStreams.get();
	if (!streams) {
		return eventHook(event);
	}
	auto that = QPointer<QWidget>();
	const auto allAreObserved = streams->events.has_consumers();
	if (allAreObserved) {
		that = rpWidget();
		streams->events.fire_copy(event);
		if (!that) {
			return true;
		}
	}
	switch (event->type()) {
	case QEvent::Show:
	case QEvent::Hide:
		if (rpWidget()->isWindow() && streams->shown.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->shown.fire_copy(!rpWidget()->isHidden());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::WindowActivate:
	case QEvent::WindowDeactivate:
		if (streams->windowActive.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->windowActive.fire_copy(rpWidget()->isActiveWindow());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::Move:
	case QEvent::Resize:
		if (streams->geometry.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->geometry.fire_copy(rpWidget()->geometry());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::ScreenChangeInternal:
		if (streams->screen.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->screen.fire_copy(rpWidget()->screen());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::Paint:
		if (streams->paint.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			const auto rect = static_cast<QPaintEvent*>(event)->rect();
			streams->paint.fire_copy(rect);
			if (!that) {
				return true;
			}
		}
		break;
	}

	return eventHook(event);
}

RpWidgetWrap::Initer::Initer(QWidget *parent, bool setZeroGeometry) {
	if (setZeroGeometry) {
		parent->setGeometry(0, 0, 0, 0);
	}
}

void RpWidgetWrap::visibilityChangedHook(bool wasVisible, bool nowVisible) {
	if (nowVisible != wasVisible) {
		if (auto streams = _eventStreams.get()) {
			streams->shown.fire_copy(nowVisible);
		}
	}
}

auto RpWidgetWrap::eventStreams() const -> EventStreams& {
	if (!_eventStreams) {
		_eventStreams = std::make_unique<EventStreams>();
	}
	return *_eventStreams;
}

} // namespace Ui
