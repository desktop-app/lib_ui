// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_window_title_win.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>

namespace Ui {
namespace Platform {

TitleWidget::TitleWidget(not_null<RpWidget*> parent)
: RpWidget(parent)
, _controls(this, st::defaultWindowTitle)
, _shadow(this, st::titleShadow) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		setGeometry(0, 0, width, _controls.st()->height);
	}, lifetime());
}

void TitleWidget::setText(const QString &text) {
	window()->setWindowTitle(text);
}

void TitleWidget::setStyle(const style::WindowTitle &st) {
	_controls.setStyle(st);
	setGeometry(0, 0, window()->width(), _controls.st()->height);
	update();
}

void TitleWidget::setResizeEnabled(bool enabled) {
	_controls.setResizeEnabled(enabled);
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	const auto active = window()->isActiveWindow();
	QPainter(this).fillRect(
		e->rect(),
		active ? _controls.st()->bgActive : _controls.st()->bg);
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

HitTestResult TitleWidget::hitTest(QPoint point) const {
	if (_controls.geometry().contains(point)) {
		return HitTestResult::SysButton;
	} else if (rect().contains(point)) {
		return HitTestResult::Caption;
	}
	return HitTestResult::None;
}

} // namespace Platform
} // namespace Ui
