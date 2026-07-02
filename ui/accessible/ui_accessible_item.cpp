// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/accessible/ui_accessible_item.h"

#include "ui/rp_widget.h"

namespace Ui::Accessible {

Item::Item(not_null<RpWidget*> parent, int index)
: _parent(parent)
, _index(index)
, _identity(parent->accessibilityChildIdentity(index)) {
}

int Item::currentIndex() const {
	const auto parent = _parent.get();
	if (!parent) {
		return -1;
	}
	if (!_identity) {
		return _index;
	}
	// Fast path: while the row stays where we last saw it, one identity
	// check confirms it - a full accessibilityChildIndexByIdentity() scan
	// on every property read would make enumeration O(n^2).
	const auto count = parent->accessibilityChildCount();
	if (_index >= 0
		&& (count < 0 || _index < count)
		&& parent->accessibilityChildIdentity(_index) == _identity) {
		return _index;
	}
	_index = parent->accessibilityChildIndexByIdentity(_identity);
	return _index;
}

bool Item::isValid() const {
	const auto parent = _parent.get();
	if (!parent || !parent->isVisible()) {
		return false;
	}
	const auto index = currentIndex();
	if (index < 0) {
		return false;
	}
	const auto count = parent->accessibilityChildCount();
	return count < 0 || index < count;
}

QObject *Item::object() const {
	return nullptr;
}

QWindow *Item::window() const {
	const auto parent = _parent.get();
	const auto w = parent ? parent->window() : nullptr;
	return w ? w->windowHandle() : nullptr;
}

QAccessible::Role Item::role() const {
	const auto parent = _parent.get();
	return parent
		? parent->accessibilityChildRole()
		: QAccessible::Role();
}

QAccessible::State Item::state() const {
	const auto parent = _parent.get();
	const auto index = parent ? currentIndex() : -1;
	return (index >= 0)
		? parent->accessibilityChildState(index)
		: QAccessible::State();
}

QString Item::text(QAccessible::Text t) const {
	const auto parent = _parent.get();
	const auto index = parent ? currentIndex() : -1;
	if (index < 0) {
		return {};
	}
	switch (t) {
	case QAccessible::Name:
		return parent->accessibilityChildName(index);
	case QAccessible::Description:
		return parent->accessibilityChildDescription(index);
	case QAccessible::Value:
		return parent->accessibilityChildValue(index);
	}
	return QString();
}

void Item::setText(QAccessible::Text t, const QString &text) {
}

QRect Item::rect() const {
	const auto parent = _parent.get();
	const auto index = parent ? currentIndex() : -1;
	if (index < 0) {
		return {};
	}
	const auto local = parent->accessibilityChildRect(index);
	if (local.isEmpty()) {
		return {};
	}
	return QRect(parent->mapToGlobal(local.topLeft()), local.size());
}

int Item::childCount() const {
	const auto parent = _parent.get();
	const auto index = parent ? currentIndex() : -1;
	if (index < 0) {
		return 0;
	}
	return parent->accessibilityChildColumnCount(index);
}

QAccessibleInterface *Item::child(int index) const {
	const auto parent = _parent.get();
	const auto current = parent ? currentIndex() : -1;
	const auto columns = (current >= 0)
		? parent->accessibilityChildColumnCount(current)
		: 0;
	if (index < 0 || index >= columns) {
		return nullptr;
	}
	if (!_subitems) {
		_subitems = std::make_unique<SubItems>();
	}
	auto &ids = _subitems->list;
	// Sub-items carrying a row identity resolve their current row themselves,
	// so the cache stays valid across reorders. Without an identity they can
	// only describe their construction-time row, so drop them when it moved.
	if (!_identity && _subitemsIndex != current) {
		ids.clear();
	}
	_subitemsIndex = current;
	if (int(ids.size()) != columns) {
		ids.resize(columns); // UniqueId handles deregistration.
	}
	if (!ids[index]) {
		ids[index] = UniqueId(
			QAccessible::registerAccessibleInterface(
				new SubItem(parent, current, index, _identity)));
	}
	return ids[index].get();
}

int Item::indexOfChild(const QAccessibleInterface *child) const {
	if (const auto subItem = dynamic_cast<const SubItem*>(child)) {
		const auto mine = _identity
			? (subItem->identity() == _identity)
			: (subItem->row() == currentIndex());
		if (mine) {
			return subItem->column();
		}
	}
	return -1;
}

QAccessibleInterface *Item::childAt(int x, int y) const {
	return nullptr;
}

QAccessibleInterface *Item::parent() const {
	return QAccessible::queryAccessibleInterface(_parent.get());
}

void *Item::interface_cast(QAccessible::InterfaceType type) {
	if (type == QAccessible::ActionInterface) {
		// Expose the action interface only when the owner opted in for this
		// child. Otherwise the Windows UIA bridge would advertise Invoke /
		// SetFocus / SelectionItem and report success while doing nothing.
		const auto parent = _parent.get();
		const auto index = parent ? currentIndex() : -1;
		if (index >= 0
			&& parent->accessibilityChildSupportsActions(index)) {
			return static_cast<QAccessibleActionInterface*>(this);
		}
	}
	return nullptr;
}

QStringList Item::actionNames() const {
	const auto parent = _parent.get();
	const auto index = parent ? currentIndex() : -1;
	if (index < 0 || !parent->accessibilityChildSupportsActions(index)) {
		return {};
	}
	const auto childState = parent->accessibilityChildState(index);
	// Pressing invokes the item's default action (e.g. open the chat).
	auto names = QStringList{ QAccessibleActionInterface::pressAction() };
	// Only advertise focusing when the row claims to be focusable,
	// matching the state() we report to the screen reader.
	if (childState.focusable) {
		names.append(QAccessibleActionInterface::setFocusAction());
	}
	// Selecting a list item maps to making it the current row. Advertise it
	// (and, more importantly, handle it in doAction) so SelectionItem.Select
	// does not silently report success: Qt routes ListItem selection through
	// toggleAction() on the Qt 5 Windows bridge.
	if (childState.selectable) {
		names.append(QAccessibleActionInterface::toggleAction());
	}
	return names;
}

void Item::doAction(const QString &actionName) {
	const auto parent = _parent.get();
	if (!parent) {
		return;
	}
	// Dispatch by stable identity, not index: the owner resolves it to the
	// current row on the main thread, so a stale action either hits the right
	// row (if it just moved) or fails safely (if it is gone). We forward the
	// identity without touching widget state here, because the Windows UIA
	// bridge invokes actions on a background thread.
	const auto identity = _identity;
	if (actionName == QAccessibleActionInterface::setFocusAction()
		|| actionName == QAccessibleActionInterface::toggleAction()) {
		parent->accessibilityChildSetFocus(identity);
	} else if (actionName == QAccessibleActionInterface::pressAction()) {
		parent->accessibilityChildActivate(identity);
	}
}

QStringList Item::keyBindingsForAction(const QString &actionName) const {
	return {};
}

SubItem::SubItem(
	not_null<RpWidget*> parent,
	int row,
	int column,
	quintptr identity)
: _parent(parent)
, _row(row)
, _column(column)
, _identity(identity) {
}

int SubItem::currentRow() const {
	const auto parent = _parent.get();
	if (!parent) {
		return -1;
	}
	if (!_identity) {
		return _row;
	}
	const auto count = parent->accessibilityChildCount();
	if (_row >= 0
		&& (count < 0 || _row < count)
		&& parent->accessibilityChildIdentity(_row) == _identity) {
		return _row;
	}
	_row = parent->accessibilityChildIndexByIdentity(_identity);
	return _row;
}

bool SubItem::isValid() const {
	const auto parent = _parent.get();
	if (_column < 0 || !parent || !parent->isVisible()) {
		return false;
	}
	const auto row = currentRow();
	if (row < 0) {
		return false;
	}
	const auto count = parent->accessibilityChildCount();
	const auto columns = parent->accessibilityChildColumnCount(row);
	return (count < 0 || row < count) && _column < columns;
}

QObject *SubItem::object() const {
	return nullptr;
}

QWindow *SubItem::window() const {
	const auto parent = _parent.get();
	const auto w = parent ? parent->window() : nullptr;
	return w ? w->windowHandle() : nullptr;
}

QAccessible::Role SubItem::role() const {
	const auto parent = _parent.get();
	return parent
		? parent->accessibilityChildSubItemRole()
		: QAccessible::Role();
}

QAccessible::State SubItem::state() const {
	return {};
}

QString SubItem::text(QAccessible::Text t) const {
	const auto parent = _parent.get();
	const auto row = parent ? currentRow() : -1;
	if (row < 0 || _column < 0) {
		return {};
	}
	switch (t) {
	case QAccessible::Name:
		return parent->accessibilityChildSubItemName(row, _column);
	case QAccessible::Value:
		return parent->accessibilityChildSubItemValue(row, _column);
	}
	return {};
}

void SubItem::setText(QAccessible::Text t, const QString &text) {
}

QRect SubItem::rect() const {
	const auto parent = _parent.get();
	const auto row = parent ? currentRow() : -1;
	if (row < 0) {
		return QRect();
	}
	const auto local = parent->accessibilityChildRect(row);
	if (local.isEmpty()) {
		return QRect();
	}
	return QRect(parent->mapToGlobal(local.topLeft()), local.size());
}

int SubItem::childCount() const {
	return 0;
}

QAccessibleInterface *SubItem::child(int index) const {
	return nullptr;
}

int SubItem::indexOfChild(const QAccessibleInterface *child) const {
	return -1;
}

QAccessibleInterface *SubItem::childAt(int x, int y) const {
	return nullptr;
}

QAccessibleInterface *SubItem::parent() const {
	const auto parent = _parent.get();
	if (!parent) {
		return nullptr;
	}
	const auto iface = QAccessible::queryAccessibleInterface(parent);
	const auto row = currentRow();
	if (iface && row >= 0 && row < iface->childCount()) {
		return iface->child(row);
	}
	return iface;
}

} // namespace Ui::Accessible
