// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/mac/ui_window_title_mac.h"

#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "base/debug_log.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>

namespace Ui {
namespace Platform {
namespace internal {

TitleControls::Layout TitleControlsLayout() {
	return TitleControls::Layout{
		.left = {
			TitleControls::Control::Close,
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
		}
	};
}

} // namespace internal

TitleWidget::TitleWidget(not_null<RpWidget*> parent, int height)
: RpWidget(parent)
, _st(&st::defaultWindowTitle)
, _shadow(this, st::titleShadow) {
	init(height);
}

TitleWidget::~TitleWidget() = default;

void TitleWidget::setText(const QString &text) {
	if (_text != text) {
		_text = text;
		_string.setText(textStyle(), text);
		update();
	}
}

void TitleWidget::setStyle(const style::WindowTitle &st) {
	_st = &st;
	update();
}

void TitleWidget::setControlsRect(const QRect &rect) {
	_controlsRight = rect.left() * 2 + rect.width();
}

bool TitleWidget::shouldBeHidden() const {
	return !_st->height;
}

const style::TextStyle &TitleWidget::textStyle() const {
	return *_textStyle;
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

	const auto setFromFont = [&](const style::font &font) {
		_textStyle = std::make_unique<style::TextStyle>(style::TextStyle{
			.font = font,
		});
	};

	const auto families = QStringList{
		u".AppleSystemUIFont"_q,
		u".SF NS Text"_q,
		u"Helvetica Neue"_q,
	};
	for (auto family : families) {
		auto font = QFont();
		font.setFamily(family);
		if (QFontInfo(font).family() == font.family()) {
			static const auto logged = [&] {
				LOG(("Title Font: %1").arg(family));
				return true;
			}();
			const auto apple = (family == u".AppleSystemUIFont"_q);
			setFromFont(style::font(
				apple ? 13 : (height * 15) / 24,
				apple ? style::internal::FontBold : 0,
				family));
			break;
		}
	}
	if (!_textStyle) {
		setFromFont(style::font(13, style::internal::FontSemibold, 0));
	}
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto active = isActiveWindow();
	p.fillRect(rect(), active ? _st->bgActive : _st->bg);

	p.setPen(active ? _st->fgActive : _st->fg);

	const auto top = (height() - _textStyle->font->height) / 2;
	if ((width() - _controlsRight * 2) < _string.maxWidth()) {
		const auto left = _controlsRight;
		_string.drawElided(p, left, top, width() - left);
	} else {
		const auto left = (width() - _string.maxWidth()) / 2;
		_string.draw(p, left, top, width() - left);
	}
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
