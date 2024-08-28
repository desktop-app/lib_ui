// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/ui_utility.h"

#include "ui/integration.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/style/style_core.h"

#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtGui/QtEvents>
#include <QWheelEvent>

#include <array>

namespace Ui {
namespace {

class WidgetCreator : public QWidget {
public:
	static void Create(not_null<QWidget*> widget) {
		volatile auto unknown = widget.get();
		static_cast<WidgetCreator*>(unknown)->create();
	}

};

void CreateWidgetStateRecursive(not_null<QWidget*> target) {
	if (!target->testAttribute(Qt::WA_WState_Created)) {
		if (!target->isWindow()) {
			CreateWidgetStateRecursive(target->parentWidget());
			WidgetCreator::Create(target);
		}
	}
}

void SendPendingEventsRecursive(QWidget *target, bool parentHiddenFlag) {
	auto wasVisible = target->isVisible();
	if (!wasVisible) {
		target->setAttribute(Qt::WA_WState_Visible, true);
	}
	if (target->testAttribute(Qt::WA_PendingMoveEvent)) {
		target->setAttribute(Qt::WA_PendingMoveEvent, false);
		QMoveEvent e(target->pos(), QPoint());
		QCoreApplication::sendEvent(target, &e);
	}
	if (target->testAttribute(Qt::WA_PendingResizeEvent)) {
		target->setAttribute(Qt::WA_PendingResizeEvent, false);
		QResizeEvent e(target->size(), QSize());
		QCoreApplication::sendEvent(target, &e);
	}

	auto removeVisibleFlag = [&] {
		return parentHiddenFlag
			|| target->testAttribute(Qt::WA_WState_Hidden);
	};

	auto &children = target->children();
	for (auto i = 0; i < children.size(); ++i) {
		auto child = children[i];
		if (child->isWidgetType()) {
			auto widget = static_cast<QWidget*>(child);
			if (!widget->isWindow()) {
				if (!widget->testAttribute(Qt::WA_WState_Created)) {
					WidgetCreator::Create(widget);
				}
				SendPendingEventsRecursive(widget, removeVisibleFlag());
			}
		}
	}

	if (removeVisibleFlag()) {
		target->setAttribute(Qt::WA_WState_Visible, false);
	}
}

} // namespace

bool AppInFocus() {
	return QApplication::focusWidget() != nullptr;
}

bool InFocusChain(not_null<const QWidget*> widget) {
	if (const auto top = widget->window()) {
		if (auto focused = top->focusWidget()) {
			return !widget->isHidden()
				&& (focused == widget
					|| widget->isAncestorOf(focused));
		}
	}
	return false;
}

void SendPendingMoveResizeEvents(not_null<QWidget*> target) {
	CreateWidgetStateRecursive(target);
	SendPendingEventsRecursive(target, !target->isVisible());
}

void MarkDirtyOpaqueChildrenRecursive(not_null<QWidget*> target) {
	target->resize(target->size()); // Calls setDirtyOpaqueRegion().
	for (const auto child : target->children()) {
		if (const auto widget = qobject_cast<QWidget*>(child)) {
			MarkDirtyOpaqueChildrenRecursive(widget);
		}
	}
}

QPixmap GrabWidget(not_null<QWidget*> target, QRect rect, QColor bg) {
	SendPendingMoveResizeEvents(target);
	if (rect.isNull()) {
		rect = target->rect();
	}

	auto result = QPixmap(rect.size() * style::DevicePixelRatio());
	result.setDevicePixelRatio(style::DevicePixelRatio());
	if (!target->testAttribute(Qt::WA_OpaquePaintEvent)) {
		result.fill(bg);
	}
	{
		QPainter p(&result);
		RenderWidget(p, target, QPoint(), rect);
	}
	return result;
}

QImage GrabWidgetToImage(not_null<QWidget*> target, QRect rect, QColor bg) {
	SendPendingMoveResizeEvents(target);
	if (rect.isNull()) {
		rect = target->rect();
	}

	auto result = QImage(
		rect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	if (!target->testAttribute(Qt::WA_OpaquePaintEvent)) {
		result.fill(bg);
	}
	if (rect.isValid()) {
		QPainter p(&result);
		RenderWidget(p, target, QPoint(), rect);
	}
	return result;
}

void RenderWidget(
		QPainter &painter,
		not_null<QWidget*> source,
		const QPoint &targetOffset,
		const QRegion &sourceRegion,
		QWidget::RenderFlags renderFlags) {
	const auto visible = source->isVisible();
	source->render(&painter, targetOffset, sourceRegion, renderFlags);
	if (!visible) {
		MarkDirtyOpaqueChildrenRecursive(source);
	}
}

void ForceFullRepaint(not_null<QWidget*> widget) {
	const auto refresher = std::make_unique<QWidget>(widget);
	refresher->setGeometry(widget->rect());
	refresher->show();
}

void ForceFullRepaintSync(not_null<QWidget*> widget) {
	const auto wm = widget->testAttribute(Qt::WA_Mapped);
	const auto wv = widget->testAttribute(Qt::WA_WState_Visible);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, true);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, true);
	ForceFullRepaint(widget);
	QEvent e(QEvent::UpdateRequest);
	QGuiApplication::sendEvent(widget, &e);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, false);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, false);
}

void PostponeCall(FnMut<void()> &&callable) {
	Integration::Instance().postponeCall(std::move(callable));
}

void SendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPoint &globalPoint) {
	if (const auto windowHandle = widget->window()->windowHandle()) {
		const auto localPoint = windowHandle->mapFromGlobal(globalPoint);
		QMouseEvent ev(type
			, localPoint
			, localPoint
			, globalPoint
			, button
			, type == QEvent::MouseButtonRelease
				? QGuiApplication::mouseButtons() ^ button
				: QGuiApplication::mouseButtons() | button
			, QGuiApplication::keyboardModifiers()
			, Qt::MouseEventSynthesizedByApplication
		);
		ev.setTimestamp(crl::now());
		QGuiApplication::sendEvent(windowHandle, &ev);
	}
}

QPixmap PixmapFromImage(QImage &&image) {
	return QPixmap::fromImage(std::move(image), Qt::ColorOnly);
}

bool IsContentVisible(
		not_null<QWidget*> widget,
		const QRect &rect) {
	Expects(widget->window()->windowHandle());

	const auto activeOrNotOverlapped = [&] {
		if (const auto active = widget->isActiveWindow()) {
			return active;
		} else if (Integration::Instance().screenIsLocked()) {
			return false;
		}

		const auto mappedRect = rect.isNull()
			? QRect(
				widget->mapTo(widget->window(), QPoint()),
				widget->size())
			: QRect(
				widget->mapTo(widget->window(), rect.topLeft()),
				rect.size());

		const auto overlapped = Platform::IsOverlapped(
			widget->window(),
			mappedRect);

		return overlapped.has_value() && !*overlapped;
	}();

	return activeOrNotOverlapped
		&& widget->isVisible()
		&& !widget->window()->isMinimized();
}

int WheelDirection(not_null<QWheelEvent*> e) {
	// Only a mouse wheel is accepted.
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
	const auto delta = e->angleDelta().y();
	const auto absDelta = std::abs(delta);
	if (absDelta != step) {
		return 0;
	}
	return (delta / absDelta);
}

QPoint MapFrom(
		not_null<QWidget*> to,
		not_null<QWidget*> from,
		QPoint point) {
	return (to->window() != from->window())
		? to->mapFromGlobal(from->mapToGlobal(point))
		: to->mapFrom(to->window(), from->mapTo(from->window(), point));
}

[[nodiscard]] QRect MapFrom(
		not_null<QWidget*> to,
		not_null<QWidget*> from,
		QRect rect) {
	return { MapFrom(to, from, rect.topLeft()), rect.size() };
}

void SetGeometryWithPossibleScreenChange(
		not_null<QWidget*> widget,
		QRect geometry) {
	Platform::SetGeometryWithPossibleScreenChange(widget, geometry);
}

} // namespace Ui
