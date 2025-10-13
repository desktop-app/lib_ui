#pragma once

#include "base/basic_types.h"
#include <QAccessible>

namespace Ui {
	class RpWidget;
} // namespace Ui

namespace Ui {

	void InstallFactory();
	void SetAccessibleName(not_null<RpWidget*> widget, const QString& name);
	void SetAccessibleRole(not_null<RpWidget*> widget, QAccessible::Role role);
	void SetAccessibleDescription(not_null<RpWidget*> widget, const QString& description);

} // namespace Ui