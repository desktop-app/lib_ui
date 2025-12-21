#pragma once

#include <QPointer>
#include <QVariant>

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

		Ui::RpWidget* _owner = nullptr;

		mutable std::vector<QPointer<Ui::RpWidget>> _children;
		mutable QPointer<Ui::RpWidget> _focusedChild;
	};

	class AccessibilityChild final {
	public:
		AccessibilityChild() = default;
		AccessibilityChild(AccessibilityChildrenManager* manager, Ui::RpWidget* child);
		~AccessibilityChild();

		AccessibilityChild(const AccessibilityChild&) = delete;
		AccessibilityChild& operator=(const AccessibilityChild&) = delete;

		// Child can call this like a real widget focus.
		void setFocus();

		// Optional if you want to rebind.
		void reset();

	private:
		AccessibilityChildrenManager* _manager = nullptr;
		QPointer<Ui::RpWidget> _child;
	};

} // namespace Ui::Accessible
