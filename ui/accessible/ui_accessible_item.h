// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/weak_qptr.h"

#include <QAccessible>
#include <QAccessibleInterface>

#include <vector>

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Accessible {

// Move-only RAII wrapper around QAccessible::Id.
// Qt's QAccessibleCache owns registered interfaces and calls `delete`
// on them via QAccessible::deleteAccessibleInterface(). This wrapper
// deregisters automatically on destruction / move-assignment, so
// vector<UniqueId>::resize() and destructor do the right thing.
class UniqueId {
public:
	UniqueId() noexcept = default;
	explicit UniqueId(QAccessible::Id id) noexcept : _id(id) {}

	~UniqueId() {
		if (_id) {
			QAccessible::deleteAccessibleInterface(_id);
		}
	}

	UniqueId(UniqueId &&other) noexcept
	: _id(std::exchange(other._id, 0)) {
	}
	UniqueId &operator=(UniqueId &&other) noexcept {
		if (this != &other) {
			if (_id) {
				QAccessible::deleteAccessibleInterface(_id);
			}
			_id = std::exchange(other._id, 0);
		}
		return *this;
	}
	UniqueId(const UniqueId &) = delete;
	UniqueId &operator=(const UniqueId &) = delete;

	[[nodiscard]] QAccessible::Id id() const noexcept {
		return _id;
	}
	[[nodiscard]] explicit operator bool() const noexcept {
		return _id != 0;
	}
	[[nodiscard]] QAccessibleInterface *get() const {
		return _id ? QAccessible::accessibleInterface(_id) : nullptr;
	}

private:
	QAccessible::Id _id = 0;

};

class Item;
class SubItem;

struct Items {
	std::vector<UniqueId> list;
};

struct SubItems {
	std::vector<UniqueId> list;
};

class Item final
	: public QAccessibleInterface
	, public QAccessibleActionInterface {
public:
	Item(not_null<RpWidget*> parent, int index);

	[[nodiscard]] int index() const {
		return _index;
	}
	[[nodiscard]] quintptr identity() const {
		return _identity;
	}

	// The row this provider currently maps to: resolved from the stable
	// identity when one is set (so the provider follows a reordered row or
	// becomes invalid when it is gone), otherwise the construction-time index.
	// The last resolved index is cached, so the common no-reorder case costs
	// one identity check instead of a full scan on every property read.
	[[nodiscard]] int currentIndex() const;

	bool isValid() const override;
	QObject *object() const override;
	QWindow *window() const override;

	QAccessible::Role role() const override;
	QAccessible::State state() const override;

	QString text(QAccessible::Text t) const override;
	void setText(QAccessible::Text t, const QString &text) override;
	QRect rect() const override;

	int childCount() const override;
	QAccessibleInterface *child(int index) const override;
	int indexOfChild(const QAccessibleInterface *child) const override;
	QAccessibleInterface *childAt(int x, int y) const override;

	QAccessibleInterface *parent() const override;

	// QAccessibleInterface.
	void *interface_cast(QAccessible::InterfaceType type) override;

	// QAccessibleActionInterface.
	QStringList actionNames() const override;
	void doAction(const QString &actionName) override;
	QStringList keyBindingsForAction(
		const QString &actionName) const override;

private:
	base::weak_qptr<RpWidget> _parent;
	mutable std::unique_ptr<SubItems> _subitems;
	mutable int _subitemsIndex = -1;
	mutable int _index = 0;
	quintptr _identity = 0;

};

class SubItem final : public QAccessibleInterface {
public:
	SubItem(
		not_null<RpWidget*> parent,
		int row,
		int column,
		quintptr identity);

	[[nodiscard]] int row() const {
		return _row;
	}
	[[nodiscard]] int column() const {
		return _column;
	}
	[[nodiscard]] quintptr identity() const {
		return _identity;
	}

	bool isValid() const override;
	QObject *object() const override;
	QWindow *window() const override;

	QAccessible::Role role() const override;
	QAccessible::State state() const override;

	QString text(QAccessible::Text t) const override;
	void setText(QAccessible::Text t, const QString &text) override;
	QRect rect() const override;

	int childCount() const override;
	QAccessibleInterface *child(int index) const override;
	int indexOfChild(const QAccessibleInterface *child) const override;
	QAccessibleInterface *childAt(int x, int y) const override;

	QAccessibleInterface *parent() const override;

private:
	// The row this cell currently belongs to, resolved from the parent row
	// identity the same way as Item::currentIndex(), so a retained cell
	// provider keeps describing its row after a reorder.
	[[nodiscard]] int currentRow() const;

	base::weak_qptr<RpWidget> _parent;
	mutable int _row = 0;
	int _column = 0;
	quintptr _identity = 0;

};

} // namespace Ui::Accessible
