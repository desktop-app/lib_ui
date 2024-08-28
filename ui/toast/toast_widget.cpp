// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast_widget.h"

#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace Ui::Toast::internal {
namespace {

[[nodiscard]] TextWithEntities ComputeText(const Config &config) {
	auto result = config.text;
	if (!config.title.isEmpty()) {
		result = Text::Bold(
			config.title
		).append('\n').append(std::move(result));
	}
	return config.singleline
		? TextUtilities::SingleLine(std::move(result))
		: result;
}

[[nodiscard]] object_ptr<RpWidget> MakeContent(
		not_null<Widget*> parent,
		Config &config) {
	if (config.content) {
		config.content->setParent(parent);
		config.content->show();
		return std::move(config.content);
	}
	auto lifetime = rpl::lifetime();
	const auto st = lifetime.make_state<style::FlatLabel>(
		st::defaultFlatLabel);
	st->style = config.st->style;
	st->textFg = st::toastFg;
	st->palette = config.st->palette;
	st->minWidth = config.padding
		? style::ConvertScale(1) // We don't really know.
		: (config.st->minWidth
			- config.st->padding.left()
			- config.st->padding.right());
	st->maxHeight = config.st->style.font->height
		* (config.singleline ? 1 : config.maxlines);

	auto result = object_ptr<FlatLabel>(parent, QString(), *st);
	const auto raw = result.data();

	raw->lifetime().add(std::move(lifetime));
	raw->setMarkedText(
		ComputeText(config),
		config.textContext ? config.textContext(raw) : std::any());
	raw->setClickHandlerFilter(std::move(config.filter));
	raw->show();

	return result;
}

[[nodiscard]] bool HasLinksOrSpoilers(const TextWithEntities &text) {
	for (const auto &entity : text.entities) {
		switch (entity.type()) {
		case EntityType::Url:
		case EntityType::CustomUrl:
		case EntityType::Email:
		case EntityType::Spoiler: return true;
		}
	}
	return false;
}

} // namespace

Widget::Widget(QWidget *parent, Config &&config)
: RpWidget(parent)
, _st(config.st)
, _roundRect(ImageRoundRadius::Large, st::toastBg)
, _attach(config.attach)
, _content(MakeContent(this, config))
, _padding(config.padding
	? std::move(config.padding) | rpl::map(rpl::mappers::_1 + _st->padding)
	: rpl::single(_st->padding) | rpl::type_erased())
, _adaptive(config.adaptive) {
	if (HasLinksOrSpoilers(config.text) || config.acceptinput) {
		setMouseTracking(true);
	} else {
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	_padding.value() | rpl::start_with_next([=] {
		parentResized();
	}, lifetime());

	show();
}

void Widget::parentResized() {
	updateGeometry();
}

void Widget::updateGeometry() {
	auto width = _st->maxWidth;
	const auto padding = _padding.current();
	const auto added = padding.left() + padding.right();
	const auto natural = _content->naturalWidth();
	const auto max = (natural > 0) ? natural : _content->width();
	accumulate_min(width, max + added);
	accumulate_min(
		width,
		parentWidget()->width() - _st->margin.left() - _st->margin.right());
	if (_adaptive) {
		width = FindNiceTooltipWidth(0, width - added, [&](int width) {
			_content->resizeToWidth(width);
			return _content->heightNoMargins();
		}) + added;
	}
	_content->resizeToWidth(width - added);
	const auto minHeight = _st->icon.empty()
		? 0
		: (_st->icon.height() + 2 * _st->iconPosition.y());
	const auto normalHeight = padding.top()
		+ _content->heightNoMargins()
		+ padding.bottom();
	const auto height = std::max(minHeight, normalHeight);
	const auto top = padding.top() + ((height - normalHeight) / 2);
	_content->moveToLeft(padding.left(), top);

	const auto rect = QRect(0, 0, width, height);
	const auto outer = parentWidget()->size();
	const auto full = QPoint(outer.width(), outer.height());
	const auto middle = QPoint(
		(outer.width() - width) / 2,
		(outer.height() - height) / 2);
	_updateShownGeometry = [=](float64 level) {
		const auto interpolated = [&](int from, int to) {
			return anim::interpolate(from, to, level);
		};
		setGeometry(rect.translated([&] {
			switch (_attach) {
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
	};
	_updateShownGeometry(_shownLevel);
}

void Widget::setShownLevel(float64 shownLevel) {
	if (_shownLevel == shownLevel) {
		return;
	}
	_shownLevel = shownLevel;
	if (_attach != RectPart::None) {
		_updateShownGeometry(_shownLevel);
	} else {
		update();
	}
}

void Widget::paintToProxy() {
	const auto ratio = devicePixelRatio();
	const auto full = size() * ratio;
	if (_shownProxy.size() != full) {
		_shownProxy = QImage(full, QImage::Format_ARGB32_Premultiplied);
	}
	_shownProxy.setDevicePixelRatio(ratio);
	_shownProxy.fill(Qt::transparent);

	auto q = QPainter(&_shownProxy);
	const auto saved = std::exchange(_shownLevel, 1.);
	Ui::RenderWidget(q, this);
	_shownLevel = saved;
}

void Widget::disableChildrenPaintOnce() {
	const auto toggle = [=](bool updatesDisabled) {
		for (const auto child : children()) {
			if (child->isWidgetType()) {
				static_cast<QWidget*>(child)->setAttribute(
					Qt::WA_UpdatesDisabled,
					updatesDisabled);
			}
		}
	};
	toggle(true);
	Ui::PostponeCall(this, [=] {
		toggle(false);
	});
}

void Widget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto opacity = (_attach == RectPart::None)
		? _shownLevel
		: 1.;
	if (opacity < 1.) {
		paintToProxy();
		p.setOpacity(opacity);
		p.drawImage(0, 0, _shownProxy);
		disableChildrenPaintOnce();
		return;
	}

	auto hq = PainterHighQualityEnabler(p);
	if (_attach == RectPart::None && _shownLevel < 1.) {
		p.setOpacity(_shownLevel);
	}
	_roundRect.paint(p, rect());

	if (!_st->icon.empty()) {
		_st->icon.paint(
			p,
			_st->iconPosition.x(),
			_st->iconPosition.y(),
			width());
	}
}

} // namespace Ui::Toast::internal
