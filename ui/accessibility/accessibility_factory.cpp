// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/accessibility/accessibility_factory.h"

#include <QtGui/QAccessible>

namespace Ui::Accessibility {

void Init() {
	QAccessible::installFactory([](const QString &key, QObject *object) {
		auto result = (QAccessibleInterface*)nullptr;
		if (!object) {
			return result;
		}

		auto event = Event(Event::Type());
		QCoreApplication::sendEvent(object, &event);
		return event.interface();
	});
}

} // namespace Ui::Accessibility
