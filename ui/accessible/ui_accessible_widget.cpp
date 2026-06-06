// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/accessible/ui_accessible_widget.h"

#include "base/debug_log.h"
#include "base/integration.h"
#include "base/timer.h"
#include "ui/accessible/ui_accessible_item.h"
#include "ui/rp_widget.h"
#include "ui/screen_reader_mode.h"

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
	Ui::ScreenReaderModeActiveValue(
	) | rpl::on_next([=](bool active) {
		_active = active;
		LOG(("Screen Reader: %1").arg(active ? "active" : "inactive"));

		cleanup();
		for (const auto &widget : _widgets) {
			widget->setFocusPolicy(active
				? widget->accessibilityFocusPolicy()
				: Qt::NoFocus);
		}
	}, _lifetime);
}

void FocusManager::registerWidget(not_null<RpWidget*> widget) {
	const auto policy = widget->accessibilityFocusPolicy();
	if (policy == Qt::NoFocus) {
		return;
	} else if (_active) {
		widget->setFocusPolicy(policy);
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
	if (type == QAccessible::SelectionInterface
		&& rp()->accessibilitySelectionList()) {
		return static_cast<QAccessibleSelectionInterface*>(this);
	}
	if (type == QAccessible::AttributesInterface
		&& rp()->accessibilityOrientation().has_value()) {
		return static_cast<QAccessibleAttributesInterface*>(this);
	}
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
	const auto widgets = rp()->accessibilityChildWidgets();
	if (!widgets.empty()) {
		return int(widgets.size());
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
	const auto widgets = rp()->accessibilityChildWidgets();
	if (!widgets.empty()) {
		return (index < int(widgets.size()))
			? QAccessible::queryAccessibleInterface(widgets[index].get())
			: nullptr;
	}
	return QAccessibleWidget::child(index);
}

int Widget::indexOfChild(const QAccessibleInterface* child) const {
	if (const auto item = dynamic_cast<const Ui::Accessible::Item*>(child)) {
		return item->currentIndex();
	}
	if (child) {
		const auto widgets = rp()->accessibilityChildWidgets();
		if (!widgets.empty()) {
			const auto object = child->object();
			for (auto i = 0; i != int(widgets.size()); ++i) {
				if (widgets[i].get() == object) {
					return i;
				}
			}
			return -1;
		}
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

	// A selection list forwards accessible focus to its current (selected) item,
	// so focusing the container lands the screen reader on a navigable item
	// rather than the inert container. Only while the container itself holds
	// focus - if a specific item has keyboard focus, fall through so it is
	// reported. Opt-in, so plain lists (e.g. message history, which tracks a
	// separate focused item) are not affected.
	if (rp()->accessibilitySelectionList()
		&& widget()->hasFocus()) {
		if (const auto selected = selectedItem(0)) {
			return selected;
		}
	}

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
	// On Qt 5 the Windows UIA bridge redirects a container's SetFocus to
	// focusChild() only for an element exposing a table interface, not for a
	// List - so focus would land on the inert container. Forward SetFocus to
	// the selected item's widget directly instead (no extra Qt patch needed).
	// Opt-in, so only a selection list (the folder strip) is affected.
	if (actionName == QAccessibleActionInterface::setFocusAction()
		&& rp()->accessibilitySelectionList()) {
		if (const auto selected = selectedItem(0)) {
			if (const auto widget = qobject_cast<QWidget*>(selected->object())) {
				widget->setFocus(Qt::OtherFocusReason);
				return;
			}
		}
	}
	QAccessibleWidget::doAction(actionName);
	base::Integration::Instance().enterFromEventLoop([&] {
		rp()->accessibilityDoAction(actionName);
	});
}

// Selection. A selection item is a child with the ListItem role reporting
// selected = active; the selected one resolves independently of focus. Plain
// buttons among the children are excluded, and a locked folder reports
// selectable = false, so it is never claimed as a successful selection.

int Widget::selectedItemCount() const {
	return int(selectedItems().size());
}

QList<QAccessibleInterface*> Widget::selectedItems() const {
	auto result = QList<QAccessibleInterface*>();
	const auto count = childCount();
	for (auto i = 0; i != count; ++i) {
		const auto item = child(i);
		if (item
			&& item->role() == QAccessible::ListItem
			&& item->state().selected) {
			result.append(item);
		}
	}
	return result;
}

QAccessibleInterface *Widget::selectedItem(int selectionIndex) const {
	// Bounds-safe: an out-of-range index (including negative) yields nullptr.
	return selectedItems().value(selectionIndex, nullptr);
}

bool Widget::isSelected(QAccessibleInterface *childItem) const {
	return childItem
		&& indexOfChild(childItem) >= 0
		&& childItem->role() == QAccessible::ListItem
		&& childItem->state().selected;
}

bool Widget::select(QAccessibleInterface *childItem) {
	// Only report success for a list item that belongs to this container and can
	// actually become selected - otherwise (e.g. a locked folder whose press
	// just opens an upsell) we must not claim the selection succeeded. A list
	// item only implements pressAction, so invoke that rather than toggleAction.
	if (!childItem
		|| indexOfChild(childItem) < 0
		|| childItem->role() != QAccessible::ListItem
		|| childItem->state().disabled
		|| !childItem->state().selectable) {
		return false;
	}
	if (const auto actions = childItem->actionInterface()) {
		actions->doAction(QAccessibleActionInterface::pressAction());
		return true;
	}
	return false;
}

bool Widget::unselect(QAccessibleInterface*) {
	return false; // A single-selection list always keeps one current item.
}

bool Widget::selectAll() {
	return false; // Single-selection container.
}

bool Widget::clear() {
	return false; // A single-selection list always keeps one current item.
}

// Attributes. Reports the container's orientation (e.g. a vertical list) so UI
// Automation can describe a horizontal/vertical orientation.

QList<QAccessible::Attribute> Widget::attributeKeys() const {
	auto result = QList<QAccessible::Attribute>();
	if (rp()->accessibilityOrientation().has_value()) {
		result.append(QAccessible::Attribute::Orientation);
	}
	return result;
}

QVariant Widget::attributeValue(QAccessible::Attribute key) const {
	if (key == QAccessible::Attribute::Orientation) {
		if (const auto orientation = rp()->accessibilityOrientation()) {
			// Plain int by design: the UIA bridge reads this back with
			// QVariant::toInt(), and Qt::Orientation isn't a registered
			// metatype here - QVariant::fromValue() of it wouldn't round-trip.
			return int(*orientation);
		}
	}
	return QVariant();
}

} // namespace Ui::Accessible
