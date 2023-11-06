// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast_widget.h"

#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/tooltip.h"
#include "ui/painter.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace Ui {
namespace Toast {
namespace internal {
namespace {

[[nodiscard]] TextWithEntities ComputeText(const Config &config) {
	auto result = config.text;
	if (!config.title.isEmpty()) {
		result = Text::Bold(
			config.title
		).append('\n').append(std::move(result));
	}
	return config.multiline
		? result
		: TextUtilities::SingleLine(std::move(result));
}

} // namespace

Widget::Widget(QWidget *parent, const Config &config)
: RpWidget(parent)
, _st(config.st)
, _roundRect(ImageRoundRadius::Large, st::toastBg)
, _slideSide(config.slideSide)
, _multiline(config.multiline)
, _dark(config.dark)
, _adaptive(config.adaptive)
, _maxTextWidth(widthWithoutPadding(_st->maxWidth))
, _maxTextHeight(
	_st->style.font->height * (_multiline ? config.maxLines : 1))
, _text(_multiline ? widthWithoutPadding(config.st->minWidth) : kQFixedMax)
, _clickHandlerFilter(config.filter) {
	const auto toastOptions = TextParseOptions{
		TextParseMultiline,
		_maxTextWidth,
		_maxTextHeight,
		Qt::LayoutDirectionAuto
	};
	_text.setMarkedText(
		_st->style,
		ComputeText(config),
		toastOptions,
		config.textContext ? config.textContext(this) : std::any());
	if (_text.hasSpoilers()) {
		const auto weak = Ui::MakeWeak(this);
		_text.setSpoilerLinkFilter([=](const ClickContext &context) {
			return (weak != nullptr);
		});
	}

	_processMouse = _text.hasLinks() || _text.hasSpoilers();
	if (_processMouse) {
		setMouseTracking(true);
	} else {
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	onParentResized();
	show();
}

void Widget::setInputUsed(bool used) {
	setAttribute(Qt::WA_TransparentForMouseEvents, !used && !_processMouse);
}

void Widget::onParentResized() {
	updateGeometry();
}

void Widget::updateGeometry() {
	auto width = _st->maxWidth;
	accumulate_min(
		width,
		_st->padding.left() + _text.maxWidth() + _st->padding.right());
	accumulate_min(
		width,
		parentWidget()->width() - _st->margin.left() - _st->margin.right());
	if (_adaptive) {
		const auto added = _st->padding.left() + _st->padding.right();
		width = FindNiceTooltipWidth(0, width - added, [&](int width) {
			return _text.countHeight(width);
		}) + added;
	}
	_textWidth = widthWithoutPadding(width);
	_textHeight = _multiline
		? qMin(_text.countHeight(_textWidth), _maxTextHeight)
		: _text.minHeight();
	const auto minHeight = _st->icon.empty()
		? 0
		: (_st->icon.height() + 2 * _st->iconPosition.y());
	const auto normalHeight = _st->padding.top()
		+ _textHeight
		+ _st->padding.bottom();
	const auto height = std::max(minHeight, normalHeight);
	_textTop = _st->padding.top() + ((height - normalHeight) / 2);
	const auto rect = QRect(0, 0, width, height);
	const auto outer = parentWidget()->size();
	const auto full = QPoint(outer.width(), outer.height());
	const auto middle = QPoint(
		(outer.width() - width) / 2,
		(outer.height() - height) / 2);
	const auto interpolated = [&](int from, int to) {
		return anim::interpolate(from, to, _shownLevel);
	};
	setGeometry(rect.translated([&] {
		switch (_slideSide) {
		case RectPart::None:
			return middle;
		case RectPart::Left:
			return QPoint(
				interpolated(-width, _st->margin.left()),
				middle.y());
		case RectPart::Top:
			return QPoint(
				middle.x(),
				interpolated(-height, _st->margin.top()));
		case RectPart::Right:
			return QPoint(
				full.x() - interpolated(0, width + _st->margin.right()),
				middle.y());
		case RectPart::Bottom:
			return QPoint(
				middle.x(),
				full.y() - interpolated(0, height + _st->margin.bottom()));
		}
		Unexpected("Slide side in Toast::Widget::updateGeometry.");
	}()));
}

void Widget::setShownLevel(float64 shownLevel) {
	_shownLevel = shownLevel;
	if (_slideSide != RectPart::None) {
		updateGeometry();
	} else {
		update();
	}
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	if (_slideSide == RectPart::None) {
		p.setOpacity(_shownLevel);
	}
	_roundRect.paint(p, rect());
	if (_dark) {
		//_roundRect.paint(p, rect());
	}

	if (!_st->icon.empty()) {
		_st->icon.paint(
			p,
			_st->iconPosition.x(),
			_st->iconPosition.y(),
			width());
	}

	p.setPen(st::toastFg);
	_text.draw(p, {
		.position = { _st->padding.left(), _textTop },
		.availableWidth = _textWidth + 1,
		.palette = &_st->palette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.elisionHeight = _maxTextHeight,
	});
}

void Widget::leaveEventHook(QEvent *e) {
	if (!_processMouse) {
		return;
	}
	if (ClickHandler::getActive()) {
		ClickHandler::setActive(nullptr);
		setCursor(style::cur_default);
		update();
	}
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	if (!_processMouse) {
		return;
	}
	const auto point = e->pos()
		- QPoint(_st->padding.left(), _textTop);
	auto request = Ui::Text::StateRequestElided();
	request.lines = (_maxTextHeight / _st->style.font->height);
	const auto state = _text.getStateElided(point, _textWidth + 1, request);
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
	if (!_processMouse || e->button() != Qt::LeftButton) {
		return;
	}
	ClickHandler::pressed();
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	if (!_processMouse || e->button() != Qt::LeftButton) {
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
