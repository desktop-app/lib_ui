#include "ui/accessible/ui_accessible_children_manager.h"

#include "ui/rp_widget.h"

#include <QAccessible>
#include <QAccessibleObject>
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

		void registerManager(const Ui::RpWidget* ownerKey, AccessibilityChildrenManager* manager) {
			if (!ownerKey || !manager) return;
			registry().insert(ownerKey, manager);
		}

		void unregisterManager(const Ui::RpWidget* ownerKey, const AccessibilityChildrenManager* manager) {
			if (!ownerKey || !manager) return;
			auto& map = registry();
			const auto it = map.find(ownerKey);
			if (it != map.end() && it.value() == manager) {
				map.erase(it);
			}
		}

		AccessibilityChildrenManager* lookupManager(const Ui::RpWidget* ownerKey) {
			if (!ownerKey) return nullptr;
			const auto it = registry().constFind(ownerKey);
			return (it == registry().constEnd()) ? nullptr : it.value();
		}

		class AccessibilityChildInterface final : public QAccessibleObject {
		public:
			explicit AccessibilityChildInterface(not_null<AccessibilityChild*> child)
				: QAccessibleObject(child.get())
				, _child(child.get()) {
			}

			QRect rect() const override {
				const auto child = _child.data();
				if (!child) return {};
				if (const auto parent = child->parentRp()) {
					const auto r = parent->rect();
					const auto topLeft = parent->mapToGlobal(r.topLeft());
					return QRect(topLeft, r.size());
				}
				return {};
			}

			QAccessible::Role role() const override {
				return QAccessible::StaticText;
			}

			QAccessible::State state() const override {
				QAccessible::State result;
				const auto child = _child.data();
				if (!child) {
					result.invisible = true;
					return result;
				}
				if (const auto parent = child->parentRp()) {
					if (const auto manager = AccessibilityChildrenManager::lookup(parent)) {
						if (manager->focusedChild() == child) {
							result.focused = true;
						}
					}
				}
				return result;
			}

			QString text(QAccessible::Text t) const override {
				const auto child = _child.data();
				if (!child) return {};
				switch (t) {
				case QAccessible::Name:
					return child->accessibleName();
				default:
					return {};
				}
			}

			int childCount() const override {
				return 0;
			}

			QAccessibleInterface* child(int) const override {
				return nullptr;
			}

			int indexOfChild(const QAccessibleInterface*) const override {
				return -1;
			}

			QAccessibleInterface* parent() const override {
				const auto child = _child.data();
				if (!child) return nullptr;
				if (const auto parentRp = child->parentRp()) {
					return QAccessible::queryAccessibleInterface(parentRp);
				}
				return nullptr;
			}

		private:
			QPointer<AccessibilityChild> _child;
		};

	} // namespace

	AccessibilityChildrenManager::AccessibilityChildrenManager(Ui::RpWidget* owner)
		: _ownerKey(owner)
		, _owner(owner) {
		registerManager(_ownerKey, this);
	}

	AccessibilityChildrenManager::~AccessibilityChildrenManager() {
		unregisterManager(_ownerKey, this);
	}

	AccessibilityChildrenManager* AccessibilityChildrenManager::lookup(const Ui::RpWidget* owner) {
		return lookupManager(owner);
	}

	AccessibilityChildrenManager* AccessibilityChildrenManager::ensure(not_null<Ui::RpWidget*> owner) {
		if (const auto existing = lookup(owner.get())) {
			return existing;
		}
		// Lifetime: deleted automatically when owner is destroyed.
		auto* created = new AccessibilityChildrenManager(owner.get());
		QObject::connect(owner.get(), &QObject::destroyed, [created] {
			delete created;
			});
		return created;
	}

	void AccessibilityChildrenManager::cleanup() const {
		_children.erase(
			std::remove_if(begin(_children), end(_children), [&](const QPointer<AccessibilityChild>& p) {
				return p.isNull();
				}),
			end(_children));
		if (_focusedChild.isNull()) {
			_focusedChild = nullptr;
		}
	}

	void AccessibilityChildrenManager::notifyReorder() {
		if (!_owner) return;
		QAccessibleEvent e(_owner.data(), QAccessible::ObjectReorder);
		QAccessible::updateAccessibility(&e);
	}

	void AccessibilityChildrenManager::notifyActiveDescendantChanged(AccessibilityChild*) {
		if (!_owner) return;
		QAccessibleEvent e(_owner.data(), QAccessible::ActiveDescendantChanged);
		// We intentionally don't set e.setChild(index) here, because this manager's
		// virtual children are merged with QWidget children in Ui::Accessible::Widget.
		QAccessible::updateAccessibility(&e);
	}

	void AccessibilityChildrenManager::registerChild(AccessibilityChild* child) {
		if (!child) return;
		cleanup();

		for (const auto& p : _children) {
			if (p.data() == child) {
				return;
			}
		}
		_children.push_back(child);
		notifyReorder();
	}

	void AccessibilityChildrenManager::unregisterChild(AccessibilityChild* child) {
		if (!child) return;
		cleanup();

		_children.erase(
			std::remove_if(begin(_children), end(_children), [&](const QPointer<AccessibilityChild>& p) {
				return p.data() == child;
				}),
			end(_children));

		if (_focusedChild.data() == child) {
			_focusedChild = nullptr;
		}
		notifyReorder();
	}

	void AccessibilityChildrenManager::setFocusedChild(AccessibilityChild* child) {
		cleanup();
		if (_focusedChild.data() == child) {
			return;
		}
		_focusedChild = child;
		notifyActiveDescendantChanged(child);
	}

	int AccessibilityChildrenManager::childCount() const {
		cleanup();
		return int(_children.size());
	}

	AccessibilityChild* AccessibilityChildrenManager::childAt(int index) const {
		cleanup();
		if (index < 0 || index >= int(_children.size())) {
			return nullptr;
		}
		return _children[index].data();
	}

	int AccessibilityChildrenManager::indexOf(const AccessibilityChild* child) const {
		cleanup();
		if (!child) return -1;
		for (auto i = 0, count = int(_children.size()); i != count; ++i) {
			if (_children[i].data() == child) {
				return i;
			}
		}
		return -1;
	}

	AccessibilityChild* AccessibilityChildrenManager::focusedChild() const {
		cleanup();
		return _focusedChild.data();
	}

	AccessibilityChild::AccessibilityChild(not_null<Ui::RpWidget*> parent)
		: QObject(parent.get())
		, _parentKey(parent.get())
		, _parent(parent.get()) {
		AccessibilityChildrenManager::ensure(parent)->registerChild(this);
	}

	AccessibilityChild::~AccessibilityChild() {
		if (const auto manager = AccessibilityChildrenManager::lookup(_parentKey)) {
			manager->unregisterChild(this);
		}
	}

	Ui::RpWidget* AccessibilityChild::parentRp() const {
		return _parent.data();
	}

	void AccessibilityChild::setFocus() {
		if (const auto manager = AccessibilityChildrenManager::lookup(_parentKey)) {
			manager->setFocusedChild(this);
		}
	}

	void AccessibilityChild::setAccessibleName(const QString& name) {
		_name = name;
	}

	QString AccessibilityChild::accessibleName() const {
		return _name;
	}

	QAccessibleInterface* AccessibilityChild::accessibilityCreate() {
		return new AccessibilityChildInterface(this);
	}

} // namespace Ui::Accessible
