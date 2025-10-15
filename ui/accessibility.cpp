#include "ui/accessibility.h"

#include "ui/rp_widget.h"
#include "base/screen_reader_state.h"
#include <QAccessibleWidget>
#include <QLineEdit>

namespace Ui {
	namespace {
		void SetupFocusManagementIfNeeded(not_null<RpWidget*> widget) {
			const auto role = widget->accessibleRole();

			if (role != QAccessible::Role::PushButton
				&& role != QAccessible::Role::Link
				&& role != QAccessible::Role::CheckBox
				&& role != QAccessible::Role::RadioButton) {
				return;
			}

			base::ScreenReaderState::Instance()->activeValue(
			) | rpl::start_with_next([widget](bool screenReaderIsActive) {
				widget->setFocusPolicy(screenReaderIsActive ? Qt::StrongFocus : Qt::NoFocus);
				}, widget->lifetime());
		}

		class CustomAccessibilityInterface final : public QAccessibleWidget, public QAccessibleTextInterface {
		public:
			using QAccessibleWidget::QAccessibleWidget;

			QAccessible::Role role() const override {
				if (const auto rpWidget = qobject_cast<const Ui::RpWidget*>(widget())) {
					const auto customRole = rpWidget->accessibleRole();
					if (customRole != QAccessible::Role::NoRole) {
						return customRole;
					}
				}
				return QAccessibleWidget::role();
			}

			QString text(QAccessible::Text t) const override {
				if (const auto rpWidget = qobject_cast<const Ui::RpWidget*>(widget())) {
					if (t == QAccessible::Name && !rpWidget->accessibleName().isEmpty()) {
						return rpWidget->accessibleName();
					}
					else if (t == QAccessible::Description && !rpWidget->accessibleDescription().isEmpty()) {
						return rpWidget->accessibleDescription();
					}
				}

				if (t == QAccessible::Value) {
					if (const auto rpWidget = qobject_cast<const Ui::RpWidget*>(widget())) {
						if (rpWidget->accessibleRole() == QAccessible::Role::EditableText) {
							return this->value();
						}
					}
				}
				return QAccessibleWidget::text(t);
			}

		protected:
			void* interface_cast(QAccessible::InterfaceType t) override;

		private:
			QString value() const;

			void selection(int selectionIndex, int* startOffset, int* endOffset) const override
			{
				if (!startOffset || !endOffset) {
					return;
				}

				if (selectionIndex == 0) {
					if (const auto lineEdit = qobject_cast<const QLineEdit*>(widget())) {
						if (lineEdit->hasSelectedText()) {
							*startOffset = lineEdit->selectionStart();
							*endOffset = *startOffset + lineEdit->selectedText().length();
							return;
						}
					}
				}

				*startOffset = -1;
				*endOffset = -1;
			}
			int selectionCount() const override
			{
				if (const auto lineEdit = qobject_cast<const QLineEdit*>(widget())) {
					return lineEdit->hasSelectedText() ? 1 : 0;
				}
				return 0;
			}
			void addSelection(int startOffset, int endOffset) override
			{
				setSelection(0, startOffset, endOffset);
			}
			void removeSelection(int selectionIndex) override
			{
				if (selectionIndex == 0) {
					if (const auto lineEdit = qobject_cast<QLineEdit*>(widget())) {
						lineEdit->deselect();
					}
				}
			}
			void setSelection(int selectionIndex, int startOffset, int endOffset) override
			{
				if (selectionIndex == 0) {
					if (const auto lineEdit = qobject_cast<QLineEdit*>(widget())) {
						lineEdit->setSelection(startOffset, endOffset - startOffset);
					}
				}
			}
			int cursorPosition() const override
			{
				if (const auto lineEdit = qobject_cast<const QLineEdit*>(widget())) {
					return lineEdit->cursorPosition();
				}
				return -1;
			}
			void setCursorPosition(int position) override
			{
				if (const auto lineEdit = qobject_cast<QLineEdit*>(widget())) {
					lineEdit->setCursorPosition(position);
				}
			}
			QString text(int startOffset, int endOffset) const override
			{
				const auto w = widget();
				if (w->hasFocus()) {
					if (const auto lineEdit = qobject_cast<const QLineEdit*>(w)) {
						return lineEdit->text().mid(startOffset, endOffset - startOffset);
					}
				}
				if (const auto rpWidget = qobject_cast<const Ui::RpWidget*>(w)) {
					if (!rpWidget->accessibleName().isEmpty()) {
						return rpWidget->accessibleName().mid(startOffset, endOffset - startOffset);
					}
				}
				if (const auto lineEdit = qobject_cast<const QLineEdit*>(w)) {
					return lineEdit->text().mid(startOffset, endOffset - startOffset);
				}
				return QString();
			}
			int characterCount() const override
			{
				const auto w = widget();
				if (w->hasFocus()) {
					if (const auto lineEdit = qobject_cast<const QLineEdit*>(w)) {
						return lineEdit->text().length();
					}
				}
				if (const auto rpWidget = qobject_cast<const Ui::RpWidget*>(w)) {
					if (!rpWidget->accessibleName().isEmpty()) {
						return rpWidget->accessibleName().length();
					}
				}
				if (const auto lineEdit = qobject_cast<const QLineEdit*>(w)) {
					return lineEdit->text().length();
				}
				return 0;
			}
			QRect characterRect(int offset) const override
			{
				return QRect();
			}
			int offsetAtPoint(const QPoint& point) const override
			{
				if (const auto lineEdit = qobject_cast<const QLineEdit*>(widget())) {
					return const_cast<QLineEdit*>(lineEdit)->cursorPositionAt(point);
				}
				return 0;
			}
			void scrollToSubstring(int startIndex, int endIndex) override
			{
				if (const auto lineEdit = qobject_cast<QLineEdit*>(widget())) {
					lineEdit->setCursorPosition(endIndex);
				}
			}
			QString attributes(int offset, int* startOffset, int* endOffset) const override
			{
				return QString();
			}
		};

		void* CustomAccessibilityInterface::interface_cast(QAccessible::InterfaceType t) {
			if (t == QAccessible::TextInterface) {
				return static_cast<QAccessibleTextInterface*>(this);
			}
			return QAccessibleWidget::interface_cast(t);
		}

		QString CustomAccessibilityInterface::value() const {
			if (const auto lineEdit = qobject_cast<const QLineEdit*>(widget())) {
				return lineEdit->text();
			}
			return QString();
		}

		QAccessibleInterface* Factory(const QString&, QObject* object) {
			if (object && object->isWidgetType()) {
				auto widget = static_cast<QWidget*>(object);
				if (auto rpWidget = qobject_cast<Ui::RpWidget*>(widget)) {
					if (rpWidget->accessibleRole() != QAccessible::Role::NoRole
						|| !rpWidget->accessibleName().isEmpty()
						|| !rpWidget->accessibleDescription().isEmpty()) {

						SetupFocusManagementIfNeeded(rpWidget);
						auto interface = new CustomAccessibilityInterface(widget);

						if (const auto lineEdit = qobject_cast<QLineEdit*>(widget)) {
							QObject::connect(lineEdit, &QLineEdit::cursorPositionChanged, lineEdit, [lineEdit](int oldPos, int newPos) {
								QAccessibleTextCursorEvent event(lineEdit, newPos);
								QAccessible::updateAccessibility(&event);
								});

							QObject::connect(lineEdit, &QLineEdit::textChanged, lineEdit, [interface, lineEdit] {
								QAccessibleValueChangeEvent event(interface, lineEdit->text());
								QAccessible::updateAccessibility(&event);
								});

							QObject::connect(lineEdit, &QLineEdit::selectionChanged, lineEdit, [interface, lineEdit] {
								int start = -1;
								int end = -1;

								if (lineEdit->hasSelectedText()) {
									start = lineEdit->selectionStart();
									end = start + lineEdit->selectedText().length();
								}

								QAccessibleTextSelectionEvent event(interface, start, end);
								QAccessible::updateAccessibility(&event);
								});
						}
						return interface;
					}
				}
			}
			return nullptr;
		}
	} // namespace

	void InstallFactory() {
		QAccessible::installFactory(Factory);
	}
} // namespace Ui