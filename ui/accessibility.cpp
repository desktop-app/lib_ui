#include "ui/accessibility.h"

#include "ui/rp_widget.h"
#include "base/screen_reader_state.h"
#include <QAccessibleWidget>

namespace Ui {
namespace {

class CustomAccessibilityInterface final : public QAccessibleWidget {
public:
	using QAccessibleWidget::QAccessibleWidget;

	QAccessible::Role role() const override {
		if (const auto rp = qobject_cast<const Ui::RpWidget*>(widget())) {
			const auto customRole = rp->accessibleRole();
			if (customRole != QAccessible::Role::NoRole) {
				return customRole;
			}
		}
		return QAccessibleWidget::role();
	}
};

void SetupFocusManagementIfNeeded(not_null<RpWidget*> widget) {
	const auto role = widget->accessibleRole();

	if (role != QAccessible::Role::Button
		&& role != QAccessible::Role::Link
		&& role != QAccessible::Role::CheckBox
		&& role != QAccessible::Role::RadioButton) {
		return;
	}

	base::ScreenReaderState::Instance()->activeValue(
	) | rpl::start_with_next([widget](bool active) {
		widget->setFocusPolicy(active ? Qt::StrongFocus : Qt::NoFocus);
	}, widget->lifetime());
}

QAccessibleInterface *Factory(const QString&, QObject* object) {
	if (const auto rpWidget = qobject_cast<Ui::RpWidget*>(object)) {
		if (rpWidget->accessibleRole() != QAccessible::Role::NoRole) {
			SetupFocusManagementIfNeeded(rpWidget);
			auto interface = new CustomAccessibilityInterface(rpWidget);

			return interface;
		}
	}
	return nullptr;
}

} // namespace

void InstallAccessibleFactory() {
	QAccessible::installFactory(Factory);
}

} // namespace Ui
