// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast_widget.h"

#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
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

[[nodiscard]] object_ptr<RpWidget> MakeBodyContent(
		not_null<RpWidget*> parent,
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
	auto context = config.textContext;
	context.repaint = [raw] { raw->update(); };
	raw->setMarkedText(ComputeText(config), context);
	raw->setClickHandlerFilter(std::move(config.filter));
	raw->show();

	return result;
}

[[nodiscard]] object_ptr<RpWidget> MakeStaticIconContent(
		not_null<RpWidget*> parent,
		const style::icon &icon) {
	auto result = object_ptr<RpWidget>(parent);
	const auto raw = result.data();
	raw->resize(icon.width(), icon.height());
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->paintRequest() | rpl::on_next([=]() {
		auto p = QPainter(raw);
		//p.fillRect(raw->rect(), QColor(0, 128, 0, 64));AssertIsDebug()
		icon.paint(p, 0, 0, raw->width());
	}, raw->lifetime());
	raw->show();
	return result;
}

[[nodiscard]] object_ptr<RpWidget> MakeIconContent(
		not_null<RpWidget*> parent,
		Config &config) {
	if (config.iconContent) {
		config.iconContent->setParent(parent);
		config.iconContent->show();
		return std::move(config.iconContent);
	}
	if (config.icon) {
		return MakeStaticIconContent(parent, *config.icon);
	}
	if (auto result = MakeIconByFactory(parent, config)) {
		result->setParent(parent);
		result->show();
		return result;
	}
	return { nullptr };
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

[[nodiscard]] style::align NormalizeToastIconAlign(style::align align) {
	const auto right = bool(align & Qt::AlignRight);
	const auto top = bool(align & Qt::AlignTop);
	const auto bottom = bool(align & Qt::AlignBottom);
	return right
		? (top
			? style::al_topright
			: bottom
			? style::al_bottomright
			: style::al_right)
		: (top
			? style::al_topleft
			: bottom
			? style::al_bottomleft
			: style::al_left);
}

[[nodiscard]] bool ToastIconOnRight(style::align align) {
	return bool(align & Qt::AlignRight);
}

[[nodiscard]] bool ToastIconOnTop(style::align align) {
	return bool(align & Qt::AlignTop);
}

[[nodiscard]] bool ToastIconOnBottom(style::align align) {
	return bool(align & Qt::AlignBottom);
}

struct ContentLayoutMetrics {
	style::align align = style::al_left;
	QMargins iconPadding;
	int iconFull = 0;
	QMargins effectiveStaticPadding;
	QMargins totalPadding;
};

[[nodiscard]] ContentLayoutMetrics ComputeContentLayoutMetrics(
		style::align align,
		const std::optional<style::margins> &iconPadding,
		const QMargins &staticPadding,
		const QMargins &extraPadding,
		const QSize &iconSize) {
	auto result = ContentLayoutMetrics();
	result.align = NormalizeToastIconAlign(align);
	result.iconPadding = iconPadding.value_or(st::defaultToastIconPadding);
	result.effectiveStaticPadding = staticPadding;
	if (!iconSize.isEmpty()) {
		result.iconFull = result.iconPadding.left()
			+ iconSize.width()
			+ result.iconPadding.right();
		if (ToastIconOnRight(result.align)) {
			result.effectiveStaticPadding.setRight(result.iconFull);
		} else {
			result.effectiveStaticPadding.setLeft(result.iconFull);
		}
	}
	result.totalPadding = result.effectiveStaticPadding + extraPadding;
	return result;
}

class Content final : public RpWidget {
public:
	Content(
		not_null<RpWidget*> parent,
		style::align align,
		std::optional<style::margins> iconPadding,
		QMargins staticPadding,
		Fn<void()> relayoutShell);

	void setBody(object_ptr<RpWidget> body);
	void setIcon(object_ptr<RpWidget> icon);
	void setExtraPaddingSource(rpl::producer<QMargins> extraPadding);

	int resizeGetHeight(int newWidth) override;

private:
	void subscribeToBody();
	void subscribeToIcon();
	void refreshNaturalWidth();
	void requestShellRelayout();
	[[nodiscard]] ContentLayoutMetrics layoutMetrics() const;

	object_ptr<RpWidget> _body = { nullptr };
	object_ptr<RpWidget> _icon = { nullptr };
	const style::align _align = style::al_left;
	const std::optional<style::margins> _iconPadding;
	const QMargins _staticPadding;
	rpl::variable<QMargins> _extraPadding;
	Fn<void()> _relayoutShell;

};

Content::Content(
	not_null<RpWidget*> parent,
	style::align align,
	std::optional<style::margins> iconPadding,
	QMargins staticPadding,
	Fn<void()> relayoutShell)
: RpWidget(parent)
, _align(NormalizeToastIconAlign(align))
, _iconPadding(std::move(iconPadding))
, _staticPadding(staticPadding)
, _extraPadding(QMargins())
, _relayoutShell(std::move(relayoutShell)) {
	show();
}

void Content::setBody(object_ptr<RpWidget> body) {
	_body = std::move(body);
	if (_body) {
		_body->setParent(this);
		_body->show();
		subscribeToBody();
	}
	refreshNaturalWidth();
}

void Content::setIcon(object_ptr<RpWidget> icon) {
	_icon = std::move(icon);
	if (_icon) {
		_icon->setParent(this);
		_icon->show();
		subscribeToIcon();
	}
	refreshNaturalWidth();
}

void Content::setExtraPaddingSource(rpl::producer<QMargins> extraPadding) {
	auto first = true;
	(extraPadding
		? std::move(extraPadding)
		: rpl::single(QMargins())
	) | rpl::on_next([=](const QMargins &padding) mutable {
		_extraPadding = padding;
		refreshNaturalWidth();
		if (std::exchange(first, false)) {
			return;
		}
		requestShellRelayout();
	}, lifetime());
}

int Content::resizeGetHeight(int newWidth) {
	const auto metrics = layoutMetrics();
	const auto availableWidth = std::max(
		newWidth - metrics.totalPadding.left() - metrics.totalPadding.right(),
		0);
	auto bodyHeight = 0;
	if (_body) {
		_body->resizeToWidth(availableWidth);
		bodyHeight = _body->heightNoMargins();
	}
	auto height = metrics.totalPadding.top()
		+ bodyHeight
		+ metrics.totalPadding.bottom();
	const auto icon = _icon.data();
	const auto iconSize = icon ? icon->size() : QSize();
	if (!iconSize.isEmpty()) {
		height = std::max(
			height,
			_extraPadding.current().top()
				+ metrics.iconPadding.top()
				+ iconSize.height()
				+ metrics.iconPadding.bottom()
				+ _extraPadding.current().bottom());
	}
	if (_body) {
		const auto bodyTop = metrics.totalPadding.top()
			+ ((height
				- metrics.totalPadding.top()
				- metrics.totalPadding.bottom()
				- bodyHeight) / 2);
		_body->moveToLeft(metrics.totalPadding.left(), bodyTop, newWidth);
	}
	if (!icon || iconSize.isEmpty()) {
		return height;
	}
	const auto iconTop = ToastIconOnTop(metrics.align)
		? (_extraPadding.current().top() + metrics.iconPadding.top())
		: ToastIconOnBottom(metrics.align)
		? (height
			- _extraPadding.current().bottom()
			- metrics.iconPadding.bottom()
			- iconSize.height())
		: (_extraPadding.current().top()
			+ ((height
				- _extraPadding.current().top()
				- _extraPadding.current().bottom()
				- iconSize.height()) / 2));
	if (ToastIconOnRight(metrics.align)) {
		icon->moveToRight(
			_extraPadding.current().right() + metrics.iconPadding.right(),
			iconTop,
			newWidth);
	} else {
		icon->moveToLeft(
			_extraPadding.current().left() + metrics.iconPadding.left(),
			iconTop,
			newWidth);
	}
	return height;
}

void Content::subscribeToBody() {
	const auto body = _body.data();
	if (!body) {
		return;
	}
	auto firstNatural = true;
	body->naturalWidthValue() | rpl::on_next([=]() mutable {
		refreshNaturalWidth();
		if (std::exchange(firstNatural, false)) {
			return;
		}
		requestShellRelayout();
	}, lifetime());
	body->sizeValue() | rpl::on_next([=]() {
		refreshNaturalWidth();
	}, lifetime());
}

void Content::subscribeToIcon() {
	const auto icon = _icon.data();
	if (!icon) {
		return;
	}
	auto firstNatural = true;
	icon->naturalWidthValue() | rpl::on_next([=]() mutable {
		refreshNaturalWidth();
		if (std::exchange(firstNatural, false)) {
			return;
		}
		requestShellRelayout();
	}, lifetime());
	auto firstSize = true;
	icon->sizeValue() | rpl::on_next([=]() mutable {
		refreshNaturalWidth();
		if (std::exchange(firstSize, false)) {
			return;
		}
		requestShellRelayout();
	}, lifetime());
}

void Content::refreshNaturalWidth() {
	const auto metrics = layoutMetrics();
	const auto body = _body.data();
	const auto bodyNatural = body
		? body->naturalWidth()
		: -1;
	const auto bodyWidth = body
		? ((bodyNatural > 0) ? bodyNatural : body->widthNoMargins())
		: 0;
	setNaturalWidth(std::max(bodyWidth, 0)
		+ metrics.totalPadding.left()
		+ metrics.totalPadding.right());
}

void Content::requestShellRelayout() {
	if (_relayoutShell) {
		_relayoutShell();
	}
}

ContentLayoutMetrics Content::layoutMetrics() const {
	const auto icon = _icon.data();
	return ComputeContentLayoutMetrics(
		_align,
		_iconPadding,
		_staticPadding,
		_extraPadding.current(),
		icon ? icon->size() : QSize());
}

[[nodiscard]] object_ptr<RpWidget> MakeContent(
		not_null<Widget*> parent,
		Config &config) {
	auto result = object_ptr<Content>(
		parent,
		config.iconAlign,
		std::move(config.iconPadding),
		config.st->padding,
		[parent] {
			parent->parentResized();
		});
	const auto raw = result.data();
	raw->setBody(MakeBodyContent(raw, config));
	raw->setIcon(MakeIconContent(raw, config));
	raw->setExtraPaddingSource(std::move(config.padding));
	return result;
}

} // namespace

Widget::Widget(QWidget *parent, Config &&config)
: RpWidget(parent)
, _st(config.st)
, _roundRect(_st->radius, st::toastBg)
, _attach(config.attach)
, _addToAttach(config.addToAttachSide
	? std::move(config.addToAttachSide)
	: rpl::single(0))
, _content(MakeContent(this, config))
, _adaptive(config.adaptive) {
	if (HasLinksOrSpoilers(config.text) || config.acceptinput) {
		setMouseTracking(true);
	} else {
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	_addToAttach.value() | rpl::on_next([=]() {
		parentResized();
	}, lifetime());

	crl::on_main_update_requests(
	) | rpl::on_next([=]() {
		if (_attach == RectPart::None && _shownLevel < 1.) {
			scheduleChildrenPaintRestore();
		}
	}, lifetime());

	show();
}

void Widget::parentResized() {
	updateGeometry();
}

void Widget::updateGeometry() {
	auto width = _st->maxWidth;
	const auto natural = _content->naturalWidth();
	const auto contentWidth = (natural > 0)
		? natural
		: _content->widthNoMargins();
	accumulate_min(width, contentWidth);
	accumulate_min(
		width,
		parentWidget()->width() - _st->margin.left() - _st->margin.right());
	width = std::max(width, 0);
	if (_adaptive) {
		width = FindNiceTooltipWidth(
			0,
			width,
			[&](int width) {
				_content->resizeToWidth(width);
				return _content->heightNoMargins();
			});
	}
	_content->resizeToWidth(width);
	const auto height = _content->heightNoMargins();
	_content->move(0, 0);

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
			const auto add = _addToAttach.current();
			switch (_attach) {
			case RectPart::None:
				return middle;
			case RectPart::Left:
				return QPoint(
					interpolated(-width, _st->margin.left() + add),
					middle.y());
			case RectPart::Top:
				return QPoint(
					middle.x(),
					interpolated(-height, _st->margin.top() + add));
			case RectPart::Right:
				return QPoint(
					full.x() - interpolated(
						0,
						width + _st->margin.right() + add),
					middle.y());
			case RectPart::Bottom:
				return QPoint(
					middle.x(),
					full.y() - interpolated(
						0,
						height + _st->margin.bottom() + add));
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
	toggleChildrenPaint(false);
	scheduleChildrenPaintRestore();
}

void Widget::toggleChildrenPaint(bool enabled) {
	_childrenPaintDisabled = !enabled;
	for (const auto child : children()) {
		if (child->isWidgetType()) {
			static_cast<QWidget*>(child)->setAttribute(
				Qt::WA_UpdatesDisabled,
				_childrenPaintDisabled);
		}
	}
}

void Widget::scheduleChildrenPaintRestore() {
	if (_childrenPaintRestoreScheduled) {
		return;
	}
	_childrenPaintRestoreScheduled = true;
	Ui::PostponeCall(this, [=] {
		_childrenPaintRestoreScheduled = false;
		if (_childrenPaintDisabled) {
			toggleChildrenPaint(true);
		}
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
	_roundRect.paint(p, rect());
}

} // namespace Ui::Toast::internal
