#pragma once

#include <QPointer>
#include <QObject>
#include <QString>

#include <vector>

class QAccessibleInterface;

namespace Ui {
	class RpWidget;
} // namespace Ui

namespace Ui::Accessible {

	class AccessibilityChild;

	class AccessibilityChildrenManager final {
	public:
		explicit AccessibilityChildrenManager(Ui::RpWidget* owner);
		~AccessibilityChildrenManager();

		AccessibilityChildrenManager(const AccessibilityChildrenManager&) = delete;
		AccessibilityChildrenManager& operator=(const AccessibilityChildrenManager&) = delete;

		void registerChild(AccessibilityChild* child);
		void unregisterChild(AccessibilityChild* child);
		void setFocusedChild(AccessibilityChild* child);

		int childCount() const;
		AccessibilityChild* childAt(int index) const;
		int indexOf(const AccessibilityChild* child) const;
		AccessibilityChild* focusedChild() const;

		// Find an existing manager for owner (returns nullptr if none).
		static AccessibilityChildrenManager* lookup(const Ui::RpWidget* owner);

		// Ensure a manager exists for owner (creates one on first use).
		static AccessibilityChildrenManager* ensure(not_null<Ui::RpWidget*> owner);

	private:
		void cleanup() const;
		void notifyReorder();
		void notifyActiveDescendantChanged(AccessibilityChild* child);

		const Ui::RpWidget* _ownerKey = nullptr;
		QPointer<Ui::RpWidget> _owner;

		mutable std::vector<QPointer<AccessibilityChild>> _children;
		mutable QPointer<AccessibilityChild> _focusedChild;
	};

	class AccessibilityChild final : public QObject {
		Q_OBJECT

	public:
		explicit AccessibilityChild(not_null<Ui::RpWidget*> parent);
		~AccessibilityChild() override;

		AccessibilityChild(const AccessibilityChild&) = delete;
		AccessibilityChild& operator=(const AccessibilityChild&) = delete;

		[[nodiscard]] Ui::RpWidget* parentRp() const;

		// Call this when this virtual child becomes the active/focused descendant.
		void setFocus();

		// Minimal metadata for screen readers.
		void setAccessibleName(const QString& name);
		[[nodiscard]] QString accessibleName() const;

		// Used by the accessibility factory.
		[[nodiscard]] QAccessibleInterface* accessibilityCreate();

	private:
		const Ui::RpWidget* _parentKey = nullptr;
		QPointer<Ui::RpWidget> _parent;
		QString _name;
	};

} // namespace Ui::Accessible