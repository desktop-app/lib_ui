#include "ui/accessible/ui_accessible_children_manager.h"

#include "ui/rp_widget.h"

#include <QAccessible>
#include <algorithm>

namespace Ui::Accessible {
	namespace {

		constexpr auto kManagerProperty = "_ui_accessibility_children_manager_ptr";

		AccessibilityChildrenManager* fromProperty(const Ui::RpWidget* owner) {
			if (!owner) return nullptr;
			const auto v = owner->property(kManagerProperty);
			if (!v.isValid()) return nullptr;
			const auto raw = static_cast<quintptr>(v.toULongLong());
			return reinterpret_cast<AccessibilityChildrenManager*>(raw);
		}

		void setProperty(Ui::RpWidget* owner, AccessibilityChildrenManager* ptr) {
			if (!owner) return;
			const auto raw = static_cast<quintptr>(reinterpret_cast<quintptr>(ptr));
			owner->setProperty(kManagerProperty, QVariant::fromValue<qulonglong>(raw));
		}

		void clearProperty(Ui::RpWidget* owner) {
			if (!owner) return;
			owner->setProperty(kManagerProperty, QVariant());
		}

	} // namespace

	AccessibilityChildrenManager::AccessibilityChildrenManager(Ui::RpWidget* owner)
		: _owner(owner) {
		setProperty(_owner, this);
	}

	AccessibilityChildrenManager::~AccessibilityChildrenManager() {
		// Clear only if we still own the property.
		if (fromProperty(_owner) == this) {
			clearProperty(_owner);
		}
	}

	AccessibilityChildrenManager* AccessibilityChildrenManager::lookup(const Ui::RpWidget* owner) {
		return fromProperty(owner);
	}

	void AccessibilityChildrenManager::cleanup() const {
		_children.erase(
			std::remove_if(
				begin(_children),
				end(_children),
				[](const QPointer<Ui::RpWidget>& p) { return !p; }),
			end(_children));

		if (_focusedChild
			&& std::find(begin(_children), end(_children), _focusedChild) == end(_children)) {
			_focusedChild = nullptr;
		}
	}

	int AccessibilityChildrenManager::indexOf(const Ui::RpWidget* child) const {
		if (!child) return -1;
		auto index = 0;
		for (const auto& p : _children) {
			if (p.data() == child) {
				return index;
			}
			++index;
		}
		return -1;
	}

	int AccessibilityChildrenManager::childCount() const {
		cleanup();
		return _children.empty() ? -1 : int(_children.size());
	}

	Ui::RpWidget* AccessibilityChildrenManager::childAt(int index) const {
		cleanup();
		if (index < 0 || index >= int(_children.size())) {
			return nullptr;
		}
		return _children[index].data();
	}

	Ui::RpWidget* AccessibilityChildrenManager::focusedChild() const {
		cleanup();
		return _focusedChild.data();
	}

	void AccessibilityChildrenManager::notifyReorder() {
		if (!_owner) return;
		QAccessibleEvent e(_owner, QAccessible::ObjectReorder);
		QAccessible::updateAccessibility(&e);
	}

	void AccessibilityChildrenManager::notifyActiveDescendantChanged(Ui::RpWidget* child) {
		if (!_owner) return;
		QAccessibleEvent e(_owner, QAccessible::ActiveDescendantChanged);
		const auto index = child ? indexOf(child) : -1;
		if (index >= 0) {
			e.setChild(index);
		}
		QAccessible::updateAccessibility(&e);
	}

	void AccessibilityChildrenManager::registerChild(Ui::RpWidget* child) {
		if (!child) return;
		cleanup();

		// avoid duplicates
		for (const auto& p : _children) {
			if (p.data() == child) {
				return;
			}
		}
		_children.push_back(child);
		notifyReorder();
	}

	void AccessibilityChildrenManager::unregisterChild(Ui::RpWidget* child) {
		if (!child) return;
		cleanup();

		_children.erase(
			std::remove_if(
				begin(_children),
				end(_children),
				[&](const QPointer<Ui::RpWidget>& p) { return p.data() == child; }),
			end(_children));

		if (_focusedChild.data() == child) {
			_focusedChild = nullptr;
		}
		notifyReorder();
	}

	void AccessibilityChildrenManager::setFocusedChild(Ui::RpWidget* child) {
		cleanup();
		if (_focusedChild.data() == child) {
			return;
		}
		_focusedChild = child;
		notifyActiveDescendantChanged(child);
	}

	// -------- AccessibilityChild (RAII wrapper) --------

	AccessibilityChild::AccessibilityChild(
		AccessibilityChildrenManager* manager,
		Ui::RpWidget* child)
		: _manager(manager)
		, _child(child) {
		if (_manager && _child) {
			_manager->registerChild(_child);
		}
	}

	AccessibilityChild::~AccessibilityChild() {
		if (_manager && _child) {
			_manager->unregisterChild(_child);
		}
	}

	void AccessibilityChild::setFocus() {
		if (_manager && _child) {
			_manager->setFocusedChild(_child);
		}
	}

	void AccessibilityChild::reset() {
		if (_manager && _child) {
			_manager->unregisterChild(_child);
		}
		_manager = nullptr;
		_child = nullptr;
	}

} // namespace Ui::Accessible
