// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QAccessibleInterface>
#include <QPointer>

#include <vector>

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Accessible {

// Forward declaration for use in Item.
class SubItem;

// Accessibility interface for virtual/painted items (not real widgets).
// Stores parent widget and index directly - all data comes from parent's
// accessibilityChild* methods.
class Item final : public QAccessibleInterface {
public:
	Item(not_null<RpWidget*> parent, int index);

	void setIndex(int index) { _index = index; }
	[[nodiscard]] int index() const { return _index; }

	// Validity.
	bool isValid() const override;
	QObject* object() const override;
	QWindow* window() const override;

	// Identity.
	QAccessible::Role role() const override;
	QAccessible::State state() const override;

	// Content.
	QString text(QAccessible::Text t) const override;
	void setText(QAccessible::Text t, const QString& text) override;
	QRect rect() const override;

	// Children (sub-items when parent has columns).
	int childCount() const override;
	QAccessibleInterface* child(int index) const override;
	int indexOfChild(const QAccessibleInterface* child) const override;
	QAccessibleInterface* childAt(int x, int y) const override;

	// Navigation.
	QAccessibleInterface* parent() const override;

private:
	not_null<RpWidget*> _parent;
	QPointer<QWidget> _parentGuard;
	int _index;
	mutable std::vector<SubItem*> _subItems;
};

// Simple accessibility interface for a column sub-item within a list item.
// Exposes column name as Name and column value as Value.
class SubItem final : public QAccessibleInterface {
public:
	SubItem(not_null<RpWidget*> parent, int row, int column);

	[[nodiscard]] int row() const { return _row; }
	[[nodiscard]] int column() const { return _column; }

	// Validity.
	bool isValid() const override;
	QObject* object() const override;
	QWindow* window() const override;

	// Identity.
	QAccessible::Role role() const override;
	QAccessible::State state() const override;

	// Content.
	QString text(QAccessible::Text t) const override;
	void setText(QAccessible::Text t, const QString& text) override;
	QRect rect() const override;

	// Children (sub-items have none).
	int childCount() const override;
	QAccessibleInterface* child(int index) const override;
	int indexOfChild(const QAccessibleInterface* child) const override;
	QAccessibleInterface* childAt(int x, int y) const override;

	// Navigation.
	QAccessibleInterface* parent() const override;

private:
	not_null<RpWidget*> _parent;
	QPointer<QWidget> _parentGuard;
	int _row;
	int _column;
};

[[nodiscard]] Item *cachedItem(not_null<RpWidget*> parent, int index);

} // namespace Ui::Accessible
