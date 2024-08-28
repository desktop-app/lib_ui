// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/unique_qptr.h"

#include <crl/crl.h>
#include <QtCore/QEvent>
#include <QtWidgets/QWidget>

class QPixmap;
class QImage;
class QWheelEvent;

template <typename Object>
class object_ptr;

namespace Ui {

template <typename Widget, typename ...Args>
inline base::unique_qptr<Widget> CreateObject(Args &&...args) {
	return base::make_unique_q<Widget>(
		nullptr,
		std::forward<Args>(args)...);
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
[[nodiscard]] bool InFocusChain(not_null<const QWidget*> widget);

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
void ForceFullRepaintSync(not_null<QWidget*> widget);

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

[[nodiscard]] QPixmap PixmapFromImage(QImage &&image);

[[nodiscard]] bool IsContentVisible(
	not_null<QWidget*> widget,
	const QRect &rect = QRect());

int WheelDirection(not_null<QWheelEvent*> e);

[[nodiscard]] QPoint MapFrom(
	not_null<QWidget*> to,
	not_null<QWidget*> from,
	QPoint point);

[[nodiscard]] QRect MapFrom(
	not_null<QWidget*> to,
	not_null<QWidget*> from,
	QRect rect);

void SetGeomtryWithPossibleScreenChange(
	not_null<QWidget*> widget,
	QRect geometry);

} // namespace Ui
