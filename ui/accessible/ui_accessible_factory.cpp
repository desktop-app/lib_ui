// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/accessible/ui_accessible_factory.h"

#include "ui/rp_widget.h"
#include "ui/widgets/fields/input_field.h"
#include "base/screen_reader_state.h"
#include <QAccessibleWidget>

namespace Ui::Accessible {
namespace {

[[nodiscard]] QAccessibleInterface *Method(const QString&, QObject *object) {
	if (const auto rpWidget = qobject_cast<Ui::RpWidget*>(object)) {
		return rpWidget->accessibilityCreate();
	}
	// The inner editor of a field is a QTextEdit, not an RpWidget.
	return InputField::CreateInnerAccessible(object);
}

void Update(QAccessibleEvent *event) {
	// Qt raises the text events of a field's inner editor in document
	// coordinates, while the editor's accessible exposes translated ones,
	// so those are replaced. Every event passes on to the platform as the
	// same call does without a handler, with this one out of the way.
	const auto translated = InputField::TranslateInnerAccessibilityEvent(
		event);
	const auto handler = QAccessible::installUpdateHandler(nullptr);
	QAccessible::updateAccessibility(translated ? translated.get() : event);
	QAccessible::installUpdateHandler(handler);
}

} // namespace

void Init() {
	QAccessible::installFactory(Method);
	QAccessible::installUpdateHandler(Update);
}

} // namespace Ui::Accessible
