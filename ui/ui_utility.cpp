// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/ui_utility.h"

#include "ui/platform/ui_platform_utility.h"
#include "ui/style/style_core.h"

#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtGui/QtEvents>
#include <QWheelEvent>
#include <private/qhighdpiscaling_p.h>

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
	{
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
			, QGuiApplication::mouseButtons() | button
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

		const auto mappedRect = QHighDpi::toNativePixels(
			rect.isNull()
				? QRect(
					widget->mapToGlobal(QPoint()),
					widget->mapToGlobal(
						QPoint(widget->width(), widget->height())))
				: QRect(
					widget->mapToGlobal(rect.topLeft()),
					widget->mapToGlobal(rect.bottomRight())),
			widget->window()->windowHandle());

		const auto overlapped = Platform::IsOverlapped(widget, mappedRect);
		return overlapped.has_value() && !*overlapped;
	}();

	return activeOrNotOverlapped
		&& widget->isVisible()
		&& !widget->window()->isMinimized();
}

void DisableCustomScaling() {
	QHighDpiScaling::setGlobalFactor(1);
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

} // namespace Ui
