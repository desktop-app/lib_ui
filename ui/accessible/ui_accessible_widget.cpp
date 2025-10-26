// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/accessible/ui_accessible_widget.h"

#include "base/screen_reader_state.h"
#include "base/timer.h"
#include "ui/rp_widget.h"

namespace Ui::Accessible {
namespace {

constexpr auto kCleanupDelay = 5 * crl::time(1000);

class FocusManager final {
public:
	FocusManager();

	void registerWidget(not_null<RpWidget*> widget);

private:
	void cleanup();

	std::vector<QPointer<RpWidget>> _widgets;
	base::Timer _cleanupTimer;
	bool _active = false;

	rpl::lifetime _lifetime;

};

FocusManager::FocusManager() : _cleanupTimer([=] { cleanup(); }) {
	base::ScreenReaderState::Instance()->activeValue(
	) | rpl::start_with_next([=](bool active) {
		_active = active;

		cleanup();
		for (const auto &widget : _widgets) {
			widget->setFocusPolicy(active ? Qt::StrongFocus : Qt::NoFocus);
		}
	}, _lifetime);
}

void FocusManager::registerWidget(not_null<RpWidget*> widget) {
	const auto role = widget->accessibilityRole();
	if (role != QAccessible::Role::Button
		&& role != QAccessible::Role::Link
		&& role != QAccessible::Role::CheckBox
		&& role != QAccessible::Role::Slider) {
		return;
	}
	if (_active) {
		widget->setFocusPolicy(Qt::StrongFocus);
	}
	_widgets.push_back(widget.get());
	if (!_cleanupTimer.isActive()) {
		_cleanupTimer.callOnce(kCleanupDelay);
	}
}

void FocusManager::cleanup() {
	_widgets.erase(
		ranges::remove_if(
			_widgets,
			[](const QPointer<RpWidget> &widget) { return !widget; }),
		end(_widgets));
}

[[nodiscard]] FocusManager &Manager() {
	static FocusManager Instance;
	return Instance;
}

} // namespace

Widget::Widget(not_null<RpWidget*> widget) : QAccessibleWidget(widget) {
	Manager().registerWidget(widget);
}

[[nodiscard]] not_null<RpWidget*> Widget::rp() const {
	return static_cast<RpWidget*>(widget());
}

QAccessible::Role Widget::role() const {
	return rp()->accessibilityRole();
}

QAccessible::State Widget::state() const {
	auto state = QAccessibleWidget::state();
	rp()->accessibilityState(state);
	return state;
}

QString Widget::text(QAccessible::Text t) const {
	switch (t) {
	case QAccessible::Name: {
		const auto result = rp()->accessibilityName();
		return result.isEmpty() ? QAccessibleWidget::text(t) : result;
	}
	case QAccessible::Description: {
		const auto result = rp()->accessibilityDescription();
		return result.isEmpty() ? QAccessibleWidget::text(t) : result;
	}
	case QAccessible::Value: {
		const auto result = rp()->accessibilityValue();
		return result.isEmpty() ? QAccessibleWidget::text(t) : result;
	}
	}
	return QAccessibleWidget::text(t);
}

} // namespace Ui::Accessible
