#pragma once
#include <QAccessibleInterface>

namespace Ui::Accessible {

    class AccessibleItemWrap {
    public:
        virtual ~AccessibleItemWrap() = default;

        [[nodiscard]] virtual QAccessible::Role accessibilityRole() const = 0;
        [[nodiscard]] virtual QRect accessibilityRect() const = 0;

        [[nodiscard]] virtual QAccessible::State accessibilityState() const {
            return QAccessible::State();
        }

        [[nodiscard]] virtual QWidget* accessibilityParentWidget() const = 0;

        [[nodiscard]] virtual int accessibilityIndex() const = 0;
    };

    class Item : public QAccessibleInterface {
    public:
        explicit Item(not_null<AccessibleItemWrap*> item);

        [[nodiscard]] int indexInParent() const {
            return _item ? _item->accessibilityIndex() : -1;
        }

        // QAccessibleInterface overrides
        QObject* object() const override;
        bool isValid() const override;
        QWindow* window() const override;
        QAccessibleInterface* parent() const override;
        QAccessibleInterface* childAt(int x, int y) const override;
        QAccessibleInterface* child(int index) const override;
        int childCount() const override;
        int indexOfChild(const QAccessibleInterface* child) const override;
        QString text(QAccessible::Text t) const override;
        void setText(QAccessible::Text t, const QString& text) override;
        QRect rect() const override;
        QAccessible::Role role() const override;
        QAccessible::State state() const override;

    private:
        AccessibleItemWrap* _item = nullptr;
    };

} // namespace Ui::Accessible