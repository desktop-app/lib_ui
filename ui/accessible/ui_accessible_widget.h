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

	QString text(QAccessible::Text t) const override;
	QAccessible::Role role() const override;
	QAccessible::State state() const override;
	QStringList actionNames() const override;
	void doAction(const QString &actionName) override;
	int childCount() const override;
	QAccessibleInterface *child(int index) const override;
	int indexOfChild(const QAccessibleInterface *child) const override;
	QAccessibleInterface *focusChild() const override;
	QAccessibleInterface *parent() const override;

};

} // namespace Ui::Accessible
