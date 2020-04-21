// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast_widget.h"

#include "ui/image/image_prepare.h"
#include "styles/palette.h"

#include <QtGui/QtEvents>

namespace Ui {
namespace Toast {
namespace internal {

Widget::Widget(QWidget *parent, const Config &config)
: TWidget(parent)
, _roundRect(ImageRoundRadius::Large, st::toastBg)
, _multiline(config.multiline)
, _dark(config.dark)
, _maxWidth((config.maxWidth > 0) ? config.maxWidth : st::toastMaxWidth)
, _padding((config.padding.left() > 0) ? config.padding : st::toastPadding)
, _maxTextWidth(widthWithoutPadding(_maxWidth))
, _maxTextHeight(
	st::toastTextStyle.font->height * (_multiline ? config.maxLines : 1))
, _text(_multiline ? widthWithoutPadding(config.minWidth) : QFIXED_MAX)
, _clickHandlerFilter(config.filter) {
	const auto toastOptions = TextParseOptions{
		TextParseMultiline,
		_maxTextWidth,
		_maxTextHeight,
		Qt::LayoutDirectionAuto
	};
	_text.setMarkedText(
		st::toastTextStyle,
		_multiline ? config.text : TextUtilities::SingleLine(config.text),
		toastOptions);

	if (_text.hasLinks()) {
		setMouseTracking(true);
	} else {
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	onParentResized();
	show();
}

void Widget::onParentResized() {
	auto newWidth = _maxWidth;
	accumulate_min(newWidth, _padding.left() + _text.maxWidth() + _padding.right());
	accumulate_min(newWidth, parentWidget()->width() - 2 * st::toastMinMargin);
	_textWidth = widthWithoutPadding(newWidth);
	const auto textHeight = _multiline
		? qMin(_text.countHeight(_textWidth), _maxTextHeight)
		: _text.minHeight();
	const auto newHeight = _padding.top() + textHeight + _padding.bottom();
	setGeometry((parentWidget()->width() - newWidth) / 2, (parentWidget()->height() - newHeight) / 2, newWidth, newHeight);
}

void Widget::setShownLevel(float64 shownLevel) {
	_shownLevel = shownLevel;
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	p.setOpacity(_shownLevel);
	_roundRect.paint(p, rect());
	if (_dark) {
		_roundRect.paint(p, rect());
	}

	p.setTextPalette(st::toastTextPalette);

	const auto lines = _maxTextHeight / st::toastTextStyle.font->height;
	p.setPen(st::toastFg);
	_text.drawElided(p, _padding.left(), _padding.top(), _textWidth + 1, lines);
}

void Widget::leaveEventHook(QEvent *e) {
	if (!_text.hasLinks()) {
		return;
	}
	if (ClickHandler::getActive()) {
		ClickHandler::setActive(nullptr);
		setCursor(style::cur_default);
		update();
	}
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	if (!_text.hasLinks()) {
		return;
	}
	const auto point = e->pos() - QPoint(_padding.left(), _padding.top());
	const auto lines = _maxTextHeight / st::toastTextStyle.font->height;
	const auto state = _text.getStateElided(point, _textWidth + 1);
	const auto was = ClickHandler::getActive();
	if (was != state.link) {
		ClickHandler::setActive(state.link);
		if ((was != nullptr) != (state.link != nullptr)) {
			setCursor(was ? style::cur_default : style::cur_pointer);
		}
		update();
	}
}

void Widget::mousePressEvent(QMouseEvent *e) {
	if (!_text.hasLinks() || e->button() != Qt::LeftButton) {
		return;
	}
	ClickHandler::pressed();
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	if (!_text.hasLinks() || e->button() != Qt::LeftButton) {
		return;
	}
	if (const auto handler = ClickHandler::unpressed()) {
		const auto button = e->button();
		if (!_clickHandlerFilter
			|| _clickHandlerFilter(handler, button)) {
			ActivateClickHandler(this, handler, button);
		}
	}
}

} // namespace internal
} // namespace Toast
} // namespace Ui
