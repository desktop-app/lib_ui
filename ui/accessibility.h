#pragma once

#include "base/basic_types.h"
#include <QAccessible>

namespace Ui {
	class RpWidget;
} // namespace Ui

namespace Ui {

	void InstallFactory();
	void setAccessibleName(not_null<RpWidget*> widget, const QString& name);
	void setAccessibleRole(not_null<RpWidget*> widget, QAccessible::Role role);
	void setAccessibleDescription(not_null<RpWidget*> widget, const QString& description);

} // namespace Ui