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

class Widget : public QAccessibleWidget {
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

};

} // namespace Ui::Accessible
