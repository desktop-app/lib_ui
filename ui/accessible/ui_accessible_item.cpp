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
, _index(index) {
}

bool Item::isValid() const {
	const auto parent = _parent.get();
	if (_index < 0 || !parent || !parent->isVisible()) {
		return false;
	}
	const auto count = parent->accessibilityChildCount();
	return count < 0 || _index < count;
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
	return (parent && _index >= 0)
		? parent->accessibilityChildState(_index)
		: QAccessible::State();
}

QString Item::text(QAccessible::Text t) const {
	const auto parent = _parent.get();
	if (_index < 0 || !parent) {
		return {};
	}
	switch (t) {
	case QAccessible::Name:
		return parent->accessibilityChildName(_index);
	case QAccessible::Description:
		return parent->accessibilityChildDescription(_index);
	case QAccessible::Value:
		return parent->accessibilityChildValue(_index);
	}
	return QString();
}

void Item::setText(QAccessible::Text t, const QString &text) {
}

QRect Item::rect() const {
	const auto parent = _parent.get();
	if (_index < 0 || !parent) {
		return {};
	}
	const auto local = parent->accessibilityChildRect(_index);
	if (local.isEmpty()) {
		return {};
	}
	return QRect(parent->mapToGlobal(local.topLeft()), local.size());
}

int Item::childCount() const {
	const auto parent = _parent.get();
	if (_index < 0 || !parent) {
		return 0;
	}
	return parent->accessibilityChildColumnCount(_index);
}

QAccessibleInterface *Item::child(int index) const {
	const auto parent = _parent.get();
	const auto columns = parent
		? parent->accessibilityChildColumnCount(_index)
		: 0;
	if (index < 0 || index >= columns) {
		return nullptr;
	}
	if (!_subitems) {
		_subitems = std::make_unique<SubItems>();
	}
	auto &ids = _subitems->list;
	if (int(ids.size()) != columns) {
		ids.resize(columns); // UniqueId handles deregistration.
	}
	if (!ids[index]) {
		ids[index] = UniqueId(
			QAccessible::registerAccessibleInterface(
				new SubItem(parent, _index, index)));
	}
	return ids[index].get();
}

int Item::indexOfChild(const QAccessibleInterface *child) const {
	if (const auto subItem = dynamic_cast<const SubItem*>(child)) {
		if (subItem->row() == _index) {
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

SubItem::SubItem(not_null<RpWidget*> parent, int row, int column)
: _parent(parent)
, _row(row)
, _column(column) {
}

bool SubItem::isValid() const {
	const auto parent = _parent.get();
	if (_row < 0 || _column < 0 || !parent || !parent->isVisible()) {
		return false;
	}
	const auto count = parent->accessibilityChildCount();
	const auto columns = parent->accessibilityChildColumnCount(_row);
	return (count < 0 || _row < count) && _column < columns;
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
	if (_row < 0 || _column < 0 || !parent) {
		return {};
	}
	switch (t) {
	case QAccessible::Name:
		return parent->accessibilityChildSubItemName(_row, _column);
	case QAccessible::Value:
		return parent->accessibilityChildSubItemValue(_row, _column);
	}
	return {};
}

void SubItem::setText(QAccessible::Text t, const QString &text) {
}

QRect SubItem::rect() const {
	const auto parent = _parent.get();
	if (_row < 0 || !parent) {
		return QRect();
	}
	const auto local = parent->accessibilityChildRect(_row);
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
	if (iface && _row >= 0 && _row < iface->childCount()) {
		return iface->child(_row);
	}
	return iface;
}

} // namespace Ui::Accessible
