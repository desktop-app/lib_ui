// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QAccessibleWidget>

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Accessible {

class Widget
	: public QAccessibleWidget
	, public QAccessibleSelectionInterface
	, public QAccessibleAttributesInterface {
public:
	explicit Widget(not_null<RpWidget*> widget);

	[[nodiscard]] not_null<RpWidget*> rp() const;

	// Interface cast.
	void *interface_cast(QAccessible::InterfaceType type) override;

	// Identity.
	QAccessible::Role role() const override;
	QAccessible::State state() const override;

	// Content.
	QString text(QAccessible::Text t) const override;

	// Children.
	int childCount() const override;
	QAccessibleInterface* child(int index) const override;
	int indexOfChild(const QAccessibleInterface* child) const override;
	QAccessibleInterface* childAt(int x, int y) const override;
	QAccessibleInterface* focusChild() const override;

	// Navigation.
	QAccessibleInterface* parent() const override;

	// Actions.
	QStringList actionNames() const override;
	void doAction(const QString &actionName) override;

	// Selection. Exposed (via interface_cast) only for a List container, so UI
	// Automation drives SelectionItem.Select() through pressAction() (a list
	// item only implements pressAction, not toggleAction) and resolves the
	// selected item from state().selected.
	int selectedItemCount() const override;
	QList<QAccessibleInterface*> selectedItems() const override;
	QAccessibleInterface *selectedItem(int selectionIndex) const override;
	bool isSelected(QAccessibleInterface *childItem) const override;
	bool select(QAccessibleInterface *childItem) override;
	bool unselect(QAccessibleInterface *childItem) override;
	bool selectAll() override;
	bool clear() override;

	// Attributes. Exposed (via interface_cast) only when the widget reports an
	// accessibilityOrientation(), so UI Automation can announce a horizontal or
	// vertical orientation.
	QList<QAccessible::Attribute> attributeKeys() const override;
	QVariant attributeValue(QAccessible::Attribute key) const override;

};

} // namespace Ui::Accessible
