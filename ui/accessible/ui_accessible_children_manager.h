#pragma once

#include <QPointer>

#include <vector>

namespace Ui {
	class RpWidget;
} // namespace Ui

namespace Ui::Accessible {

	class AccessibilityChildrenManager final {
	public:
		explicit AccessibilityChildrenManager(Ui::RpWidget* owner);
		~AccessibilityChildrenManager();

		AccessibilityChildrenManager(const AccessibilityChildrenManager&) = delete;
		AccessibilityChildrenManager& operator=(const AccessibilityChildrenManager&) = delete;

		void registerChild(Ui::RpWidget* child);
		void unregisterChild(Ui::RpWidget* child);
		void setFocusedChild(Ui::RpWidget* child);

		int childCount() const;
		Ui::RpWidget* childAt(int index) const;
		int indexOf(const Ui::RpWidget* child) const;
		Ui::RpWidget* focusedChild() const;

		// Used by RpWidget's default accessibilityChild* methods.
		static AccessibilityChildrenManager* lookup(const Ui::RpWidget* owner);

	private:
		void cleanup() const;
		void notifyReorder();
		void notifyActiveDescendantChanged(Ui::RpWidget* child);

		QPointer<Ui::RpWidget> _owner;

		mutable std::vector<QPointer<Ui::RpWidget>> _children;
		mutable QPointer<Ui::RpWidget> _focusedChild;
	};

	class AccessibilityChild final {
	public:
		AccessibilityChild() = default;
		explicit AccessibilityChild(not_null<Ui::RpWidget*> parent);
		~AccessibilityChild();

		AccessibilityChild(const AccessibilityChild&) = delete;
		AccessibilityChild& operator=(const AccessibilityChild&) = delete;

		void registerChild(not_null<Ui::RpWidget*> child);
		void setFocus();
		void reset();

	private:
		QPointer<Ui::RpWidget> _parent;
		QPointer<Ui::RpWidget> _child;
	};

} // namespace Ui::Accessible
