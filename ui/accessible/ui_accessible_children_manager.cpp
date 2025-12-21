#include "ui/accessible/ui_accessible_children_manager.h"

#include "ui/rp_widget.h"

#include <QAccessible>
#include <QHash>
#include <QObject>
#include <algorithm>

namespace Ui::Accessible {
	namespace {

		using Registry = QHash<const Ui::RpWidget*, AccessibilityChildrenManager*>;

		Registry& registry() {
			static Registry map;
			return map;
		}

		void registerManager(Ui::RpWidget* owner, AccessibilityChildrenManager* manager) {
			if (!owner || !manager) return;
			auto& map = registry();
			// If this triggers, something is creating multiple managers for the same widget.
			Assert(!map.contains(owner) || map.value(owner) == manager);
			map.insert(owner, manager);

			// Make sure we don't keep a stale key if the widget is destroyed before the manager.
			QObject::connect(owner, &QObject::destroyed, owner, [](QObject* obj) {
				registry().remove(static_cast<Ui::RpWidget*>(obj));
				});
		}

		void unregisterManager(const Ui::RpWidget* owner, const AccessibilityChildrenManager* manager) {
			if (!owner || !manager) return;
			auto& map = registry();
			const auto it = map.find(owner);
			if (it != map.end() && it.value() == manager) {
				map.erase(it);
			}
		}

		AccessibilityChildrenManager* lookupManager(const Ui::RpWidget* owner) {
			if (!owner) return nullptr;
			const auto it = registry().constFind(owner);
			return (it == registry().constEnd()) ? nullptr : it.value();
		}

	} // namespace

	AccessibilityChildrenManager::AccessibilityChildrenManager(Ui::RpWidget* owner)
		: _owner(owner) {
		registerManager(_owner.data(), this);
	}

	AccessibilityChildrenManager::~AccessibilityChildrenManager() {
		unregisterManager(_owner.data(), this);
	}

	AccessibilityChildrenManager* AccessibilityChildrenManager::lookup(const Ui::RpWidget* owner) {
		return lookupManager(owner);
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
		QAccessibleEvent e(_owner.data(), QAccessible::ObjectReorder);
		QAccessible::updateAccessibility(&e);
	}

	void AccessibilityChildrenManager::notifyActiveDescendantChanged(Ui::RpWidget* child) {
		if (!_owner) return;
		QAccessibleEvent e(_owner.data(), QAccessible::ActiveDescendantChanged);
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
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> child)
		: _parent(parent.get())
		, _child(child.get()) {

		const auto manager = AccessibilityChildrenManager::lookup(_parent.data());
		Assert(manager != nullptr);
		if (manager) {
			manager->registerChild(_child.data());
		}
	}

	AccessibilityChild::~AccessibilityChild() {
		const auto parent = _parent.data();
		const auto child = _child.data();
		if (!parent || !child) {
			return;
		}
		if (const auto manager = AccessibilityChildrenManager::lookup(parent)) {
			manager->unregisterChild(child);
		}
	}

	void AccessibilityChild::setFocus() {
		const auto parent = _parent.data();
		const auto child = _child.data();
		if (!parent || !child) {
			return;
		}
		if (const auto manager = AccessibilityChildrenManager::lookup(parent)) {
			manager->setFocusedChild(child);
		}
	}

	void AccessibilityChild::reset() {
		const auto parent = _parent.data();
		const auto child = _child.data();
		if (parent && child) {
			if (const auto manager = AccessibilityChildrenManager::lookup(parent)) {
				manager->unregisterChild(child);
			}
		}
		_parent = nullptr;
		_child = nullptr;
	}

} // namespace Ui::Accessible
