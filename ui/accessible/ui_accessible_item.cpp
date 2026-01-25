#include "ui/accessible/ui_accessible_item.h"
#include "ui/rp_widget.h"
#include <QWidget>

namespace Ui::Accessible {

    bool Item::isValid() const {
        if (!_item) return false;

        const auto widget = _item->accessibilityParentWidget();
        if (!widget) return false;

        const auto index = _item->accessibilityIndex();
        if (index < 0) return false;
        if (const auto rp = qobject_cast<Ui::RpWidget*>(widget)) {
            const auto count = rp->accessibilityChildCount();
            if (count >= 0 && index >= count) {
                return false;
            }
        }
        return true;
    }

    QRect Item::rect() const {
        if (!_item) return QRect();

        const auto widget = _item->accessibilityParentWidget();
        const auto local = _item->accessibilityRect();

        if (!widget) return QRect();

        const auto global = widget->mapToGlobal(local.topLeft());
        return QRect(global, local.isEmpty() ? QSize(1, 1) : local.size());
    }

    Item::Item(not_null<AccessibleItemWrap*> item) : _item(item.get()) {
    }

    QObject* Item::object() const {
        return nullptr; // Items are not QObjects
    }

    QWindow* Item::window() const {
        const auto widget = _item ? _item->accessibilityParentWidget() : nullptr;
        if (!widget) return nullptr;
        const auto w = widget->window();
        return w ? w->windowHandle() : nullptr;
    }

    QAccessibleInterface* Item::parent() const {
        const auto widget = _item ? _item->accessibilityParentWidget() : nullptr;
        return widget ? QAccessible::queryAccessibleInterface(widget) : nullptr;
    }

    QAccessibleInterface* Item::child(int index) const {
        return nullptr; // Items don't have children by default
    }

    int Item::childCount() const {
        return 0;
    }

    int Item::indexOfChild(const QAccessibleInterface* child) const {
        return -1;
    }

    QString Item::text(QAccessible::Text t) const {
        if (!_item) return QString();

        const auto widget = _item->accessibilityParentWidget();
        const auto rp = widget ? qobject_cast<Ui::RpWidget*>(widget) : nullptr;
        const auto index = _item->accessibilityIndex();

        if (rp && index >= 0) {
            if (t == QAccessible::Name) {
                const auto s = rp->accessibilityChildName(index);
                if (!s.isEmpty()) return s;
            }
            else if (t == QAccessible::Description) {
                const auto s = rp->accessibilityChildDescription(index);
                if (!s.isEmpty()) return s;
            }
            else if (t == QAccessible::Value) {
                const auto s = rp->accessibilityChildValue(index);
                if (!s.isEmpty()) return s;
            }
        }

            return QString();
    }

    QAccessible::Role Item::role() const {
        return _item ? _item->accessibilityRole() : QAccessible::NoRole;
    }

    QAccessible::State Item::state() const {
        return _item ? _item->accessibilityState() : QAccessible::State();
    }

    void Item::setText(QAccessible::Text t, const QString& text) {
    }

    QAccessibleInterface* Item::childAt(int x, int y) const {
        return nullptr;
    }

} // namespace Ui::Accessible