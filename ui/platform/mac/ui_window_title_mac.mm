// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_window_title_mac.h"

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

TitleWidget::TitleWidget(not_null<RpWidget*> parent, int height)
: RpWidget(parent)
, _st(&st::defaultWindowTitle)
, _shadow(this, st::titleShadow) {
	init(height);
}

void TitleWidget::setText(const QString &text) {
	if (_text != text) {
		_text = text;
		update();
	}
}

void TitleWidget::setStyle(const style::WindowTitle &st) {
	_st = &st;
	update();
}

bool TitleWidget::shouldBeHidden() const {
	return !_st->height;
}

QString TitleWidget::text() const {
	return _text;
}

not_null<RpWidget*> TitleWidget::window() const {
	return static_cast<RpWidget*>(parentWidget());
}

void TitleWidget::init(int height) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	window()->widthValue(
	) | rpl::start_with_next([=](int width) {
		setGeometry(0, 0, width, height);
	}, lifetime());

	const auto families = QStringList{
		QString(".SF NS Text"),
		QString("Helvetica Neue")
	};
	for (auto family : families) {
		_font.setFamily(family);
		if (QFontInfo(_font).family() == _font.family()) {
			break;
		}
	}

	if (QFontInfo(_font).family() == _font.family()) {
		_font.setPixelSize((height * 15) / 24);
	} else {
		_font = st::normalFont;
	}
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	const auto active = isActiveWindow();
	p.fillRect(rect(), active ? _st->bgActive : _st->bg);

	p.setFont(_font);
	p.setPen(active ? _st->fgActive : _st->fg);
	p.drawText(rect(), _text, style::al_center);
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	const auto window = parentWidget();
	if (window->windowState() == Qt::WindowMaximized) {
		window->setWindowState(Qt::WindowNoState);
	} else {
		window->setWindowState(Qt::WindowMaximized);
	}
}

} // namespace Platform
} // namespace Ui
