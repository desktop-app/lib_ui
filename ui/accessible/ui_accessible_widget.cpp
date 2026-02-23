// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/accessible/ui_accessible_widget.h"

#include "base/debug_log.h"
#include "base/integration.h"
#include "base/screen_reader_state.h"
#include "base/timer.h"
#include "ui/accessible/ui_accessible_item.h"
#include "ui/rp_widget.h"

#include <algorithm>

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
	) | rpl::on_next([=](bool active) {
		_active = active;
		LOG(("Screen Reader: %1").arg(active ? "active" : "inactive"));

		cleanup();
		for (const auto &widget : _widgets) {
			widget->setFocusPolicy(active ? Qt::TabFocus : Qt::NoFocus);
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
		widget->setFocusPolicy(Qt::TabFocus);
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

not_null<RpWidget*> Widget::rp() const {
	return static_cast<RpWidget*>(widget());
}

// Interface cast.

void *Widget::interface_cast(QAccessible::InterfaceType type) {
	return QAccessibleWidget::interface_cast(type);
}

// Identity.

QAccessible::Role Widget::role() const {
	return rp()->accessibilityRole();
}

QAccessible::State Widget::state() const {
	auto result = QAccessibleWidget::state();
	rp()->accessibilityState().writeTo(result);
	return result;
}

// Content.

QString Widget::text(QAccessible::Text t) const {
	const auto result = QAccessibleWidget::text(t);
	if (!result.isEmpty()) {
		return result;
	}
	switch (t) {
	case QAccessible::Name: {
		return rp()->accessibilityName();
	}
	case QAccessible::Description: {
		return rp()->accessibilityDescription();
	}
	case QAccessible::Value: {
		return rp()->accessibilityValue();
	}
	}
	return result;
}

// Children.

int Widget::childCount() const {
	const auto count = rp()->accessibilityChildCount();
	if (count >= 0) {
		return count;
	}
	return QAccessibleWidget::childCount();
}

QAccessibleInterface* Widget::child(int index) const {
	if (index < 0) {
		return nullptr;
	}
	if (const auto customInterface = rp()->accessibilityChildInterface(index)) {
		return customInterface;
	}
	return QAccessibleWidget::child(index);
}

int Widget::indexOfChild(const QAccessibleInterface* child) const {
	if (const auto item = dynamic_cast<const Ui::Accessible::Item*>(child)) {
		return item->index();
	}
	return QAccessibleWidget::indexOfChild(child);
}

QAccessibleInterface* Widget::childAt(int x, int y) const {
	const auto count = rp()->accessibilityChildCount();
	if (count >= 0) {
		const QPoint p(x, y);
		for (int i = 0; i < count; ++i) {
			if (const auto iface = rp()->accessibilityChildInterface(i)) {
				if (iface->rect().contains(p)) {
					return iface;
				}
			}
		}
		return nullptr;
	}
	return QAccessibleWidget::childAt(x, y);
}

QAccessibleInterface* Widget::focusChild() const {
	// Guard against re-entrancy which can cause infinite loops.
	// Qt's QAccessibleWidget::focusChild() may trigger accessibility
	// queries that call back into focusChild().
	static thread_local int ReentrancyDepth = 0;
	if (ReentrancyDepth > 0) {
		return nullptr;
	}
	++ReentrancyDepth;
	struct Guard { ~Guard() { --ReentrancyDepth; } } guard;

	// Only handle focus child for widgets with custom accessibility children.
	// For other widgets (containers, scroll areas), delegate to Qt immediately.
	const auto count = rp()->accessibilityChildCount();
	if (count < 0) {
		// No custom children - let Qt handle it normally.
		return QAccessibleWidget::focusChild();
	}

	if (!widget()->hasFocus()) {
		return nullptr;
	}

	// Iterate through children to find focused one (Qt standard approach).
	for (int i = 0; i < count; ++i) {
		if (const auto iface = rp()->accessibilityChildInterface(i)) {
			const auto s = iface->state();
			if (s.focused || s.active) {
				return iface;
			}
		}
	}

	// Has custom children but none focused - return null.
	return nullptr;
}

// Navigation.

QAccessibleInterface* Widget::parent() const {
	if (const auto parentRp = rp()->accessibilityParent()) {
		if (auto iface = QAccessible::queryAccessibleInterface(parentRp)) {
			return iface;
		}
	}
	return QAccessibleWidget::parent();
}

// Actions.

QStringList Widget::actionNames() const {
	return QAccessibleWidget::actionNames()
		+ rp()->accessibilityActionNames();
}

void Widget::doAction(const QString &actionName) {
	QAccessibleWidget::doAction(actionName);
	base::Integration::Instance().enterFromEventLoop([&] {
		rp()->accessibilityDoAction(actionName);
	});
}

} // namespace Ui::Accessible
