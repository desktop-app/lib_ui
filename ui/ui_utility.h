// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"
#include "ui/rect_part.h"
#include "ui/integration.h"

#include <QtCore/QEvent>

class QPixmap;
class QImage;

enum class RectPart;
using RectParts = base::flags<RectPart>;

template <typename Object>
class object_ptr;

namespace Ui {
namespace details {

template <typename Value>
class AttachmentOwner : public QObject {
public:
	template <typename ...Args>
	AttachmentOwner(QObject *parent, Args &&...args)
	: QObject(parent)
	, _value(std::forward<Args>(args)...) {
	}

	not_null<Value*> value() {
		return &_value;
	}

private:
	Value _value;

};

} // namespace details

template <typename Widget, typename ...Args>
inline base::unique_qptr<Widget> CreateObject(Args &&...args) {
	return base::make_unique_q<Widget>(
		nullptr,
		std::forward<Args>(args)...);
}

template <typename Value, typename Parent, typename ...Args>
inline Value *CreateChild(
		Parent *parent,
		Args &&...args) {
	Expects(parent != nullptr);

	if constexpr (std::is_base_of_v<QObject, Value>) {
		return new Value(parent, std::forward<Args>(args)...);
	} else {
		return CreateChild<details::AttachmentOwner<Value>>(
			parent,
			std::forward<Args>(args)...)->value();
	}
}

template <typename Value>
inline not_null<details::AttachmentOwner<std::decay_t<Value>>*> WrapAsQObject(
		not_null<QObject*> parent,
		Value &&value) {
	return CreateChild<details::AttachmentOwner<std::decay_t<Value>>>(
		parent.get(),
		std::forward<Value>(value));
}

inline void DestroyChild(QWidget *child) {
	delete child;
}

template <typename ...Args>
inline auto Connect(Args &&...args) {
	return QObject::connect(std::forward<Args>(args)...);
}

template <typename Value>
inline not_null<std::decay_t<Value>*> AttachAsChild(
		not_null<QObject*> parent,
		Value &&value) {
	return WrapAsQObject(parent, std::forward<Value>(value))->value();
}

[[nodiscard]] bool AppInFocus();

[[nodiscard]] inline bool InFocusChain(not_null<const QWidget*> widget) {
	if (const auto top = widget->window()) {
		if (auto focused = top->focusWidget()) {
			return !widget->isHidden()
				&& (focused == widget
					|| widget->isAncestorOf(focused));
		}
	}
	return false;
}

template <typename ChildWidget>
inline ChildWidget *AttachParentChild(
		not_null<QWidget*> parent,
		const object_ptr<ChildWidget> &child) {
	if (const auto raw = child.data()) {
		raw->setParent(parent);
		raw->show();
		return raw;
	}
	return nullptr;
}

void SendPendingMoveResizeEvents(not_null<QWidget*> target);

[[nodiscard]] QPixmap GrabWidget(
	not_null<QWidget*> target,
	QRect rect = QRect(),
	QColor bg = QColor(255, 255, 255, 0));

[[nodiscard]] QImage GrabWidgetToImage(
	not_null<QWidget*> target,
	QRect rect = QRect(),
	QColor bg = QColor(255, 255, 255, 0));

void RenderWidget(
	QPainter &painter,
	not_null<QWidget*> source,
	const QPoint &targetOffset = QPoint(),
	const QRegion &sourceRegion = QRegion(),
	QWidget::RenderFlags renderFlags
	= QWidget::DrawChildren | QWidget::IgnoreMask);

void ForceFullRepaint(not_null<QWidget*> widget);

void PostponeCall(FnMut<void()> &&callable);

template <
	typename Guard,
	typename Callable,
	typename GuardTraits = crl::guard_traits<std::decay_t<Guard>>,
	typename = std::enable_if_t<
	sizeof(GuardTraits) != crl::details::dependent_zero<GuardTraits>>>
inline void PostponeCall(Guard && object, Callable && callable) {
	return PostponeCall(crl::guard(
		std::forward<Guard>(object),
		std::forward<Callable>(callable)));
}

void SendSynteticMouseEvent(
	QWidget *widget,
	QEvent::Type type,
	Qt::MouseButton button,
	const QPoint &globalPoint);

inline void SendSynteticMouseEvent(
		QWidget *widget,
		QEvent::Type type,
		Qt::MouseButton button) {
	return SendSynteticMouseEvent(widget, type, button, QCursor::pos());
}

template <typename Widget>
QPointer<Widget> MakeWeak(Widget *object) {
	return QPointer<Widget>(object);
}

template <typename Widget>
QPointer<const Widget> MakeWeak(const Widget *object) {
	return QPointer<const Widget>(object);
}

template <typename Widget>
QPointer<Widget> MakeWeak(not_null<Widget*> object) {
	return QPointer<Widget>(object.get());
}

template <typename Widget>
QPointer<const Widget> MakeWeak(not_null<const Widget*> object) {
	return QPointer<const Widget>(object.get());
}

[[nodiscard]] QPixmap PixmapFromImage(QImage &&image);

[[nodiscard]] bool IsContentVisible(
    not_null<QWidget*> widget,
    const QRect &rect = QRect());

void DisableCustomScaling();

} // namespace Ui
