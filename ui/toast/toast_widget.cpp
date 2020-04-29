// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast_widget.h"

#include "ui/image/image_prepare.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace Ui {
namespace Toast {
namespace internal {

Widget::Widget(QWidget *parent, const Config &config)
: TWidget(parent)
, _st(config.st)
, _roundRect(ImageRoundRadius::Large, st::toastBg)
, _multiline(config.multiline)
, _dark(config.dark)
, _maxTextWidth(widthWithoutPadding(_st->maxWidth))
, _maxTextHeight(
	config.st->style.font->height * (_multiline ? config.maxLines : 1))
, _text(_multiline ? widthWithoutPadding(config.st->minWidth) : QFIXED_MAX)
, _clickHandlerFilter(config.filter) {
	const auto toastOptions = TextParseOptions{
		TextParseMultiline,
		_maxTextWidth,
		_maxTextHeight,
		Qt::LayoutDirectionAuto
	};
	_text.setMarkedText(
		_st->style,
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
	auto newWidth = _st->maxWidth;
	accumulate_min(
		newWidth,
		_st->padding.left() + _text.maxWidth() + _st->padding.right());
	accumulate_min(
		newWidth,
		parentWidget()->width() - _st->margin.left() - _st->margin.right());
	_textWidth = widthWithoutPadding(newWidth);
	const auto textHeight = _multiline
		? qMin(_text.countHeight(_textWidth), _maxTextHeight)
		: _text.minHeight();
	const auto newHeight = _st->padding.top()
		+ textHeight
		+ _st->padding.bottom();
	setGeometry(
		(parentWidget()->width() - newWidth) / 2,
		(parentWidget()->height() - newHeight) / 2,
		newWidth,
		newHeight);
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

	p.setTextPalette(_st->palette);

	const auto lines = _maxTextHeight / _st->style.font->height;
	p.setPen(st::toastFg);
	_text.drawElided(
		p,
		_st->padding.left(),
		_st->padding.top(),
		_textWidth + 1,
		lines);
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
	const auto point = e->pos()
		- QPoint(_st->padding.left(), _st->padding.top());
	const auto lines = _maxTextHeight / _st->style.font->height;
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

int Widget::widthWithoutPadding(int w) const {
	return w - _st->padding.left() - _st->padding.right();
}

} // namespace internal
} // namespace Toast
} // namespace Ui
