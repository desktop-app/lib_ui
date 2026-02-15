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
, _parentGuard(parent)
, _index(index) {
}

// Validity.

bool Item::isValid() const {
	if (!_parentGuard || _index < 0) {
		return false;
	}
	if (!_parent->isVisible()) {
		return false;
	}
	const auto count = _parent->accessibilityChildCount();
	return count < 0 || _index < count;
}

QObject* Item::object() const {
	return nullptr;
}

QWindow* Item::window() const {
	const auto w = _parent->window();
	return w ? w->windowHandle() : nullptr;
}

// Identity.

QAccessible::Role Item::role() const {
	return _parent->accessibilityChildRole();
}

QAccessible::State Item::state() const {
	if (_index < 0) {
		return QAccessible::State();
	}
	return _parent->accessibilityChildState(_index);
}

// Content.

QString Item::text(QAccessible::Text t) const {
	if (_index < 0) {
		return QString();
	}
	switch (t) {
	case QAccessible::Name:
		return _parent->accessibilityChildName(_index);
	case QAccessible::Description:
		return _parent->accessibilityChildDescription(_index);
	case QAccessible::Value:
		return _parent->accessibilityChildValue(_index);
	}
	return QString();
}

void Item::setText(QAccessible::Text t, const QString& text) {
}

QRect Item::rect() const {
	if (_index < 0) {
		return QRect();
	}
	const auto local = _parent->accessibilityChildRect(_index);
	if (local.isEmpty()) {
		return QRect();
	}
	return QRect(_parent->mapToGlobal(local.topLeft()), local.size());
}

// Children.

int Item::childCount() const {
	return _parent->accessibilityChildColumnCount(_index);
}

QAccessibleInterface* Item::child(int index) const {
	const auto columns = _parent->accessibilityChildColumnCount(_index);
	if (index < 0 || index >= columns) {
		return nullptr;
	}
	if (static_cast<int>(_subItems.size()) != columns) {
		_subItems.clear();
		_subItems.resize(columns, nullptr);
	}
	if (!_subItems[index]) {
		_subItems[index] = new SubItem(_parent, _index, index);
	}
	return _subItems[index];
}

int Item::indexOfChild(const QAccessibleInterface* child) const {
	if (const auto subItem = dynamic_cast<const SubItem*>(child)) {
		if (subItem->row() == _index) {
			return subItem->column();
		}
	}
	return -1;
}

QAccessibleInterface* Item::childAt(int x, int y) const {
	return nullptr;
}

// Navigation.

QAccessibleInterface* Item::parent() const {
	return QAccessible::queryAccessibleInterface(_parent.get());
}

// Sub-item implementation.

SubItem::SubItem(not_null<RpWidget*> parent, int row, int column)
: _parent(parent)
, _parentGuard(parent)
, _row(row)
, _column(column) {
}

bool SubItem::isValid() const {
	if (!_parentGuard || _row < 0 || _column < 0) {
		return false;
	}
	if (!_parent->isVisible()) {
		return false;
	}
	const auto count = _parent->accessibilityChildCount();
	const auto columns = _parent->accessibilityChildColumnCount(_row);
	return (count < 0 || _row < count) && _column < columns;
}

QObject* SubItem::object() const {
	return nullptr;
}

QWindow* SubItem::window() const {
	const auto w = _parent->window();
	return w ? w->windowHandle() : nullptr;
}

QAccessible::Role SubItem::role() const {
	return _parent->accessibilityChildSubItemRole();
}

QAccessible::State SubItem::state() const {
	return QAccessible::State();
}

QString SubItem::text(QAccessible::Text t) const {
	if (_row < 0 || _column < 0) {
		return QString();
	}
	switch (t) {
	case QAccessible::Name:
		return _parent->accessibilityChildSubItemName(_row, _column);
	case QAccessible::Value:
		return _parent->accessibilityChildSubItemValue(_row, _column);
	}
	return QString();
}

void SubItem::setText(QAccessible::Text t, const QString& text) {
}

QRect SubItem::rect() const {
	if (_row < 0) {
		return QRect();
	}
	const auto local = _parent->accessibilityChildRect(_row);
	if (local.isEmpty()) {
		return QRect();
	}
	return QRect(_parent->mapToGlobal(local.topLeft()), local.size());
}

int SubItem::childCount() const {
	return 0;
}

QAccessibleInterface* SubItem::child(int index) const {
	return nullptr;
}

int SubItem::indexOfChild(const QAccessibleInterface* child) const {
	return -1;
}

QAccessibleInterface* SubItem::childAt(int x, int y) const {
	return nullptr;
}

QAccessibleInterface* SubItem::parent() const {
	const auto iface = QAccessible::queryAccessibleInterface(_parent.get());
	if (iface && _row >= 0 && _row < iface->childCount()) {
		return iface->child(_row);
	}
	return iface;
}

Item *cachedItem(not_null<RpWidget*> parent, int index) {
	static thread_local QPointer<QWidget> lastWidget;
	static thread_local std::unordered_map<int, Item*> items;

	if (lastWidget.data() != parent) {
		for (auto &[_, item] : items) {
			delete item;
		}
		items.clear();
		lastWidget = parent;
	}

	auto &item = items[index];
	if (!item) {
		item = new Item(parent, index);
	} else {
		item->setIndex(index);
	}
	return item;
}

} // namespace Ui::Accessible
