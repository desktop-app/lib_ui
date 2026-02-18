// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/accessible/ui_accessible_factory.h"

#include "ui/rp_widget.h"
#include "base/screen_reader_state.h"
#include <QAccessibleWidget>

namespace Ui::Accessible {
namespace {

[[nodiscard]] QAccessibleInterface *Method(const QString&, QObject *object) {
	const auto rpWidget = qobject_cast<Ui::RpWidget*>(object);
	return rpWidget ? rpWidget->accessibilityCreate() : nullptr;
}

} // namespace

void Init() {
	QAccessible::installFactory(Method);
}

} // namespace Ui::Accessible
