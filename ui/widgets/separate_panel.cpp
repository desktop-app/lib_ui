// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/separate_panel.h"

#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/layers/box_content.h"
#include "ui/layers/layer_widget.h"
#include "ui/layers/show.h"
#include "ui/style/style_core_palette.h"
#include "ui/painter.h"
#include "base/platform/base_platform_info.h"
#include "base/debug_log.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

class PanelShow final : public Show {
public:
	explicit PanelShow(not_null<SeparatePanel*> panel);
	~PanelShow();

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

private:
	const QPointer<SeparatePanel> _panel;

};

[[nodiscard]] std::unique_ptr<style::palette> MakeAdjustedPalette(
		QColor color) {
	auto result = std::make_unique<style::palette>();
	*result = *style::main_palette::get();

	const auto set = [](const style::color &color, QColor value) {
		color.set(
			uchar(value.red()),
			uchar(value.green()),
			uchar(value.blue()),
			uchar(value.alpha()));
	};

	const auto contrast = 2.5;
	const auto luminance = 0.2126 * color.redF()
		+ 0.7152 * color.greenF()
		+ 0.0722 * color.blueF();
	const auto textColor = (luminance > 0.5)
		? QColor(0, 0, 0)
		: QColor(255, 255, 255);
	const auto textLuminance = (luminance > 0.5) ? 0 : 1;
	const auto adaptiveOpacity = (luminance - textLuminance + contrast)
		/ contrast;
	const auto opacity = std::clamp(adaptiveOpacity, 0.5, 0.64);
	auto buttonColor = textColor;
	buttonColor.setAlphaF(opacity);
	auto rippleColor = textColor;
	rippleColor.setAlphaF(opacity * 0.1);

	set(result->windowFg(), textColor);
	set(result->boxTitleCloseFg(), buttonColor);
	set(result->boxTitleCloseFgOver(), buttonColor);
	set(result->windowBgOver(), rippleColor);

	result->finalize();
	return result;
}

PanelShow::PanelShow(not_null<SeparatePanel*> panel)
: _panel(panel.get()) {
}

PanelShow::~PanelShow() = default;

void PanelShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const {
	using UniqueLayer = std::unique_ptr<LayerWidget>;
	using ObjectBox = object_ptr<BoxContent>;
	if (auto layerWidget = std::get_if<UniqueLayer>(&layer)) {
		if (const auto panel = _panel.data()) {
			panel->showLayer(std::move(*layerWidget), options, animated);
		}
	} else if (auto box = std::get_if<ObjectBox>(&layer)) {
		if (const auto panel = _panel.data()) {
			panel->showBox(std::move(*box), options, animated);
		}
	} else if (const auto panel = _panel.data()) {
		panel->hideLayer(animated);
	}
}

not_null<QWidget*> PanelShow::toastParent() const {
	const auto panel = _panel.data();

	Ensures(panel != nullptr);
	return panel;
}

bool PanelShow::valid() const {
	return (_panel.data() != nullptr);
}

PanelShow::operator bool() const {
	return valid();
}

} // namespace

SeparatePanel::SeparatePanel(SeparatePanelArgs &&args)
: RpWidget(args.parent)
, _close(this, st::separatePanelClose)
, _back(this, object_ptr<IconButton>(this, st::separatePanelBack))
, _body(this)
, _titleHeight(st::separatePanelTitleHeight) {
	setMouseTracking(true);
	setWindowIcon(QGuiApplication::windowIcon());
	initControls();
	initLayout(args);
}

SeparatePanel::~SeparatePanel() = default;

void SeparatePanel::setTitle(rpl::producer<QString> title) {
	_title.create(this, std::move(title), st::separatePanelTitle);
	updateTitleColors();
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_title->show();
	updateTitleGeometry(width());
}

void SeparatePanel::setTitleHeight(int height) {
	_titleHeight = height;
	updateControlsGeometry();
}

void SeparatePanel::initControls() {
	widthValue(
	) | rpl::start_with_next([=](int width) {
		_back->moveToLeft(_padding.left(), _padding.top());
		_close->moveToRight(_padding.right(), _padding.top());
		if (_title) {
			updateTitleGeometry(width);
		}
	}, lifetime());

	_back->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_titleLeft.start(
			[=] { updateTitlePosition(); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _back->lifetime());
	_back->hide(anim::type::instant);
	_titleLeft.stop();

	_back->raise();
	_close->raise();
}

void SeparatePanel::updateTitleButtonColors(not_null<IconButton*> button) {
	if (!_titleOverridePalette) {
		_titleOverrideStyles.remove(button);
		button->setIconOverride(nullptr, nullptr);
		button->setRippleColorOverride(nullptr);
		return;
	}
	const auto &st = button->st();
	auto &updated = _titleOverrideStyles[button];
	updated = std::make_unique<style::IconButton>(st);
	updated->icon = st.icon.withPalette(*_titleOverridePalette);
	updated->iconOver = st.iconOver.withPalette(*_titleOverridePalette);
	updated->ripple.color = _titleOverridePalette->windowBgOver();
	button->setIconOverride(&updated->icon, &updated->iconOver);
	button->setRippleColorOverride(&updated->ripple.color);
}

void SeparatePanel::updateTitleColors() {
	_title->setTextColorOverride(_titleOverridePalette
		? _titleOverridePalette->windowFg()->c
		: std::optional<QColor>());
}

void SeparatePanel::overrideTitleColor(std::optional<QColor> color) {
	if (_titleOverrideColor == color) {
		return;
	}
	_titleOverrideColor = color;
	_titleOverrideBorderParts = _titleOverrideColor
		? createBorderImage(*_titleOverrideColor)
		: QPixmap();
	_titleOverridePalette = color
		? MakeAdjustedPalette(*color)
		: nullptr;
	updateTitleButtonColors(_back->entity());
	updateTitleButtonColors(_close.data());
	if (_menuToggle) {
		updateTitleButtonColors(_menuToggle.data());
	}
	if (_title) {
		updateTitleColors();
	}
	if (!_titleOverridePalette) {
		_titleOverrideStyles.clear();
	}
	update();
}

void SeparatePanel::updateTitleGeometry(int newWidth) {
	_title->resizeToWidth(newWidth
		- _padding.left() - _back->width()
		- _padding.right() - _close->width()
		- (_menuToggle ? _menuToggle->width() : 0));
	updateTitlePosition();
}

void SeparatePanel::updateTitlePosition() {
	if (!_title) {
		return;
	}
	const auto progress = _titleLeft.value(_back->toggled() ? 1. : 0.);
	const auto left = anim::interpolate(
		st::separatePanelTitleLeft,
		_back->width() + st::separatePanelTitleSkip,
		progress);
	_title->moveToLeft(
		_padding.left() + left,
		_padding.top() + st::separatePanelTitleTop);
}

rpl::producer<> SeparatePanel::backRequests() const {
	return rpl::merge(
		_back->entity()->clicks() | rpl::to_empty,
		_synteticBackRequests.events());
}

rpl::producer<> SeparatePanel::closeRequests() const {
	return rpl::merge(
		_close->clicks() | rpl::to_empty,
		_userCloseRequests.events());
}

rpl::producer<> SeparatePanel::closeEvents() const {
	return _closeEvents.events();
}

void SeparatePanel::setBackAllowed(bool allowed) {
	if (allowed != _back->toggled()) {
		_back->toggle(allowed, anim::type::normal);
	}
}

void SeparatePanel::setMenuAllowed(
		Fn<void(const Menu::MenuCallback&)> fill) {
	_menuToggle.create(this, st::separatePanelMenu);
	updateTitleButtonColors(_menuToggle.data());
	_menuToggle->show();
	_menuToggle->setClickedCallback([=] { showMenu(fill); });

	widthValue(
	) | rpl::start_with_next([=](int width) {
		_menuToggle->moveToRight(
			_padding.right() + _close->width(),
			_padding.top());
	}, _menuToggle->lifetime());
}

void SeparatePanel::showMenu(Fn<void(const Menu::MenuCallback&)> fill) {
	const auto created = createMenu(_menuToggle);
	if (!created) {
		return;
	}
	fill(Menu::CreateAddActionCallback(_menu));
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->setForcedOrigin(PanelAnimation::Origin::TopRight);
		_menu->popup(mapToGlobal(QPoint(
			(width()
				- _padding.right()
				- _close->width()
				+ st::separatePanelMenuPosition.x()),
			st::separatePanelMenuPosition.y())));
	}
}

bool SeparatePanel::createMenu(not_null<IconButton*> button) {
	if (_menu) {
		return false;
	}
	_menu = base::make_unique_q<PopupMenu>(this, st::popupMenuWithIcons);
	_menu->setDestroyedCallback([
		weak = MakeWeak(this),
			weakButton = MakeWeak(button),
			menu = _menu.get()]{
		if (weak && weak->_menu == menu) {
			if (weakButton) {
				weakButton->setForceRippled(false);
			}
		}
		});
	button->setForceRippled(true);
	return true;
}

void SeparatePanel::setHideOnDeactivate(bool hideOnDeactivate) {
	_hideOnDeactivate = hideOnDeactivate;
	if (!_hideOnDeactivate) {
		showAndActivate();
	} else if (!isActiveWindow()) {
		LOG(("Export Info: Panel Hide On Inactive Change."));
		hideGetDuration();
	}
}

void SeparatePanel::showAndActivate() {
	if (isHidden()) {
		while (const auto widget = QApplication::activePopupWidget()) {
			if (!widget->close()) {
				break;
			}
		}
	}
	toggleOpacityAnimation(true);
	raise();
	setWindowState(windowState() | Qt::WindowActive);
	activateWindow();
	setFocus();
}

void SeparatePanel::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		crl::on_main(this, [=] {
			if (_back->toggled()) {
				_synteticBackRequests.fire({});
			} else {
				_userCloseRequests.fire({});
			}
		});
	}
	return RpWidget::keyPressEvent(e);
}

bool SeparatePanel::eventHook(QEvent *e) {
	if (e->type() == QEvent::WindowDeactivate && _hideOnDeactivate) {
		LOG(("Export Info: Panel Hide On Inactive Window."));
		hideGetDuration();
	}
	return RpWidget::eventHook(e);
}

void SeparatePanel::initLayout(const SeparatePanelArgs &args) {
	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::WindowStaysOnTopHint
		| Qt::NoDropShadowWindowHint
		| Qt::Dialog);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	validateBorderImage();
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		validateBorderImage();
		ForceFullRepaint(this);
	}, lifetime());

	if (args.onAllSpaces) {
		Platform::InitOnTopPanel(this);
	}
}

void SeparatePanel::validateBorderImage() {
	_borderParts = createBorderImage(st::windowBg->c);
}

QPixmap SeparatePanel::createBorderImage(QColor color) const {
	const auto shadowPadding = st::callShadow.extend;
	const auto cacheSize = st::separatePanelBorderCacheSize;
	auto cache = QImage(
		cacheSize * style::DevicePixelRatio(),
		cacheSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(style::DevicePixelRatio());
	cache.fill(Qt::transparent);
	{
		auto p = QPainter(&cache);
		auto inner = QRect(0, 0, cacheSize, cacheSize).marginsRemoved(
			shadowPadding);
		Shadow::paint(p, inner, cacheSize, st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(color);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(
			myrtlrect(inner),
			st::callRadius,
			st::callRadius);
	}
	return PixmapFromImage(std::move(cache));
}

void SeparatePanel::toggleOpacityAnimation(bool visible) {
	if (_visible == visible) {
		return;
	}

	_visible = visible;
	if (_useTransparency) {
		if (_animationCache.isNull()) {
			showControls();
			_animationCache = GrabWidget(this);
			hideChildren();
		}
		_opacityAnimation.start(
			[this] { opacityCallback(); },
			_visible ? 0. : 1.,
			_visible ? 1. : 0.,
			st::separatePanelDuration,
			_visible ? anim::easeOutCirc : anim::easeInCirc);
	}
	if (isHidden() && _visible) {
		show();
	}
}

void SeparatePanel::opacityCallback() {
	update();
	if (!_visible && !_opacityAnimation.animating()) {
		finishAnimating();
	}
}

void SeparatePanel::finishAnimating() {
	_animationCache = QPixmap();
	if (_visible) {
		showControls();
		if (_inner) {
			_inner->setFocus();
		}
	} else {
		finishClose();
	}
}

void SeparatePanel::showControls() {
	showChildren();
	if (!_back->toggled()) {
		_back->setVisible(false);
	}
}

void SeparatePanel::finishClose() {
	hide();
	crl::on_main(this, [=] {
		if (isHidden() && !_visible && !_opacityAnimation.animating()) {
			LOG(("Export Info: Panel Closed."));
			_closeEvents.fire({});
		}
	});
}

int SeparatePanel::hideGetDuration() {
	LOG(("Export Info: Panel Hide Requested."));
	toggleOpacityAnimation(false);
	if (_animationCache.isNull()) {
		finishClose();
		return 0;
	}
	return st::separatePanelDuration;
}

void SeparatePanel::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	Expects(box != nullptr);

	ensureLayerCreated();
	_layer->showBox(std::move(box), options, animated);
}

void SeparatePanel::showLayer(
		std::unique_ptr<LayerWidget> layer,
		LayerOptions options,
		anim::type animated) {
	Expects(layer != nullptr);

	ensureLayerCreated();
	_layer->showLayer(std::move(layer), options, animated);
}

void SeparatePanel::hideLayer(anim::type animated) {
	if (_layer) {
		_layer->hideAll(animated);
	}
}

base::weak_ptr<Toast::Instance> SeparatePanel::showToast(
		Toast::Config &&config) {
	return PanelShow(this).showToast(std::move(config));
}

base::weak_ptr<Toast::Instance> SeparatePanel::showToast(
		TextWithEntities &&text,
		crl::time duration) {
	return PanelShow(this).showToast(std::move(text), duration);
}

base::weak_ptr<Toast::Instance> SeparatePanel::showToast(
		const QString &text,
		crl::time duration) {
	return PanelShow(this).showToast(text, duration);
}

std::shared_ptr<Show> SeparatePanel::uiShow() {
	return std::make_shared<PanelShow>(this);
}

void SeparatePanel::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer = base::make_unique_q<LayerStackWidget>(
		_body,
		crl::guard(this, [=] { return std::make_shared<PanelShow>(this); }));
	_layer->setHideByBackgroundClick(false);
	_layer->move(0, 0);
	_body->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_layer->resize(size);
	}, _layer->lifetime());
	_layer->hideFinishEvents(
	) | rpl::filter([=] {
		return _layer != nullptr; // Last hide finish is sent from destructor.
	}) | rpl::start_with_next([=] {
		destroyLayer();
	}, _layer->lifetime());
}

void SeparatePanel::destroyLayer() {
	if (!_layer) {
		return;
	}

	auto layer = base::take(_layer);
	const auto resetFocus = InFocusChain(layer);
	if (resetFocus) {
		setFocus();
	}
	layer = nullptr;
}

RpWidget *SeparatePanel::inner() const {
	return _inner.get();
}

void SeparatePanel::showInner(base::unique_qptr<RpWidget> inner) {
	Expects(!size().isEmpty());

	auto old = base::take(_inner);
	_inner = std::move(inner);
	old = nullptr; // Make sure in old destructor inner() != old.

	_inner->setParent(_body);
	_inner->move(0, 0);
	_body->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_inner->resize(size);
	}, _inner->lifetime());
	_inner->show();

	if (_layer) {
		_layer->raise();
	}

	showAndActivate();
}

void SeparatePanel::focusInEvent(QFocusEvent *e) {
	crl::on_main(this, [=] {
		if (_layer) {
			_layer->setInnerFocus();
		} else if (_inner && !_inner->isHidden()) {
			_inner->setFocus();
		}
	});
}

void SeparatePanel::setInnerSize(QSize size) {
	Expects(!size.isEmpty());

	if (rect().isEmpty()) {
		initGeometry(size);
	} else {
		updateGeometry(size);
	}
}

QRect SeparatePanel::innerGeometry() const {
	return _body->geometry();
}

void SeparatePanel::initGeometry(QSize size) {
	const auto active = QApplication::activeWindow();
	const auto available = !active
		? QGuiApplication::primaryScreen()->availableGeometry()
		: active->screen()->availableGeometry();
	const auto parentGeometry = (active
		&& active->isVisible()
		&& active->isActiveWindow())
		? active->geometry()
		: available;

	auto center = parentGeometry.center();
	if (size.height() > available.height()) {
		size = QSize(size.width(), available.height());
	}
	if (center.x() + size.width() / 2
		> available.x() + available.width()) {
		center.setX(
			available.x() + available.width() - size.width() / 2);
	}
	if (center.x() - size.width() / 2 < available.x()) {
		center.setX(available.x() + size.width() / 2);
	}
	if (center.y() + size.height() / 2
		> available.y() + available.height()) {
		center.setY(
			available.y() + available.height() - size.height() / 2);
	}
	if (center.y() - size.height() / 2 < available.y()) {
		center.setY(available.y() + size.height() / 2);
	}
	_useTransparency = Platform::TranslucentWindowsSupported();
	_padding = _useTransparency
		? st::callShadow.extend
		: style::margins(
			st::lineWidth,
			st::lineWidth,
			st::lineWidth,
			st::lineWidth);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	const auto rect = [&] {
		const QRect initRect(QPoint(), size);
		return initRect.translated(center - initRect.center()).marginsAdded(_padding);
	}();
	move(rect.topLeft());
	setFixedSize(rect.size());
	updateControlsGeometry();
}

void SeparatePanel::updateGeometry(QSize size) {
	setFixedSize(
		_padding.left() + size.width() + _padding.right(),
		_padding.top() + size.height() + _padding.bottom());
	updateControlsGeometry();
	update();
}

void SeparatePanel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void SeparatePanel::updateControlsGeometry() {
	const auto top = _padding.top() + _titleHeight;
	_body->setGeometry(
		_padding.left(),
		top,
		width() - _padding.left() - _padding.right(),
		height() - top - _padding.bottom());
}

void SeparatePanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (!_animationCache.isNull()) {
		auto opacity = _opacityAnimation.value(_visible ? 1. : 0.);
		if (!_opacityAnimation.animating()) {
			finishAnimating();
			if (isHidden()) return;
		} else {
			p.setOpacity(opacity);

			PainterHighQualityEnabler hq(p);
			auto marginRatio = (1. - opacity) / 5;
			auto marginWidth = qRound(width() * marginRatio);
			auto marginHeight = qRound(height() * marginRatio);
			p.drawPixmap(
				rect().marginsRemoved(
					QMargins(
						marginWidth,
						marginHeight,
						marginWidth,
						marginHeight)),
				_animationCache,
				QRect(QPoint(0, 0), _animationCache.size()));
			return;
		}
	}

	if (_useTransparency) {
		paintShadowBorder(p);
	} else {
		paintOpaqueBorder(p);
	}
}

void SeparatePanel::paintShadowBorder(QPainter &p) const {
	const auto factor = style::DevicePixelRatio();
	const auto size = st::separatePanelBorderCacheSize;
	const auto part1 = size / 3;
	const auto part2 = size - part1;
	const auto corner = QSize(part1, part1) * factor;
	const auto radius = st::callRadius;

	const auto &header = _titleOverrideColor
		? _titleOverrideBorderParts
		: _borderParts;
	const auto topleft = QRect(QPoint(0, 0), corner);
	p.drawPixmap(QRect(0, 0, part1, part1), header, topleft);

	const auto topright = QRect(QPoint(part2, 0) * factor, corner);
	p.drawPixmap(QRect(width() - part1, 0, part1, part1), header, topright);

	const auto top = QRect(
		QPoint(part1, 0) * factor,
		QSize(part2 - part1, _padding.top() + radius) * factor);
	p.drawPixmap(
		QRect(part1, 0, width() - 2 * part1, _padding.top() + radius),
		header,
		top);

	const auto bottomleft = QRect(QPoint(0, part2) * factor, corner);
	p.drawPixmap(
		QRect(0, height() - part1, part1, part1),
		_borderParts,
		bottomleft);

	const auto bottomright = QRect(QPoint(part2, part2) * factor, corner);
	p.drawPixmap(
		QRect(width() - part1, height() - part1, part1, part1),
		_borderParts,
		bottomright);

	const auto bottom = QRect(
		QPoint(part1, size - _padding.bottom() - radius) * factor,
		QSize(part2 - part1, _padding.bottom() + radius) * factor);
	p.drawPixmap(
		QRect(
			part1,
			height() - _padding.bottom() - radius,
			width() - 2 * part1,
			_padding.bottom() + radius),
		_borderParts,
		bottom);

	const auto fillLeft = [&](int from, int till, const auto &parts) {
		const auto left = QRect(
			QPoint(0, part1) * factor,
			QSize(_padding.left(), part2 - part1) * factor);
		p.drawPixmap(
			QRect(0, from, _padding.left(), till - from),
			parts,
			left);
	};
	const auto fillRight = [&](int from, int till, const auto &parts) {
		const auto right = QRect(
			QPoint(size - _padding.right(), part1) * factor,
			QSize(_padding.right(), part2 - part1) * factor);
		p.drawPixmap(
			QRect(
				width() - _padding.right(),
				from,
				_padding.right(),
				till - from),
			parts,
			right);
	};
	const auto fillBody = [&](int from, int till, QColor color) {
		p.fillRect(
			_padding.left(),
			from,
			width() - _padding.left() - _padding.right(),
			till - from,
			color);
	};
	const auto bg = st::windowBg->c;
	if (_titleOverrideColor) {
		const auto niceOverscroll = ::Platform::IsMac();
		const auto top = niceOverscroll
			? (height() / 2)
			: (_padding.top() + _titleHeight);
		fillLeft(part1, top, _titleOverrideBorderParts);
		fillLeft(top, height() - part1, _borderParts);
		fillRight(part1, top, _titleOverrideBorderParts);
		fillRight(top, height() - part1, _borderParts);
		fillBody(_padding.top() + radius, top, *_titleOverrideColor);
		fillBody(top, height() - _padding.bottom() - radius, bg);
	} else {
		fillLeft(part1, height() - part1, _borderParts);
		fillRight(part1, height() - part1, _borderParts);
		fillBody(
			_padding.top() + radius,
			height() - _padding.bottom() - radius,
			bg);
	}
}

void SeparatePanel::paintOpaqueBorder(QPainter &p) const {
	const auto border = st::windowShadowFgFallback;
	p.fillRect(0, 0, width(), _padding.top(), border);
	p.fillRect(
		myrtlrect(
			0,
			_padding.top(),
			_padding.left(),
			height() - _padding.top()),
		border);
	p.fillRect(
		myrtlrect(
			width() - _padding.right(),
			_padding.top(),
			_padding.right(),
			height() - _padding.top()),
		border);
	p.fillRect(
		_padding.left(),
		height() - _padding.bottom(),
		width() - _padding.left() - _padding.right(),
		_padding.bottom(),
		border);

	const auto fillBody = [&](int from, int till, QColor color) {
		p.fillRect(
			_padding.left(),
			from,
			width() - _padding.left() - _padding.right(),
			till - from,
			color);
	};
	const auto bg = st::windowBg->c;
	if (_titleOverrideColor) {
		const auto half = height() / 2;
		fillBody(_padding.top(), half, *_titleOverrideColor);
		fillBody(half, height() - _padding.bottom(), bg);
	} else {
		fillBody(_padding.top(), height() - _padding.bottom(), bg);
	}
}

void SeparatePanel::closeEvent(QCloseEvent *e) {
	e->ignore();
	_userCloseRequests.fire({});
}

void SeparatePanel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(
		_padding.left(),
		_padding.top(),
		width() - _padding.left() - _padding.right(),
		_titleHeight);
	if (e->button() == Qt::LeftButton) {
		if (dragArea.contains(e->pos())) {
			const auto dragViaSystem = [&] {
				if (windowHandle()->startSystemMove()) {
					SendSynteticMouseEvent(
						this,
						QEvent::MouseButtonRelease,
						Qt::LeftButton);
					return true;
				}
				return false;
			}();
			if (!dragViaSystem) {
				_dragging = true;
				_dragStartMousePosition = e->globalPos();
				_dragStartMyPosition = QPoint(x(), y());
			}
		} else if (!rect().contains(e->pos()) && _hideOnDeactivate) {
			LOG(("Export Info: Panel Hide On Click."));
			hideGetDuration();
		}
	}
}

void SeparatePanel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition
				+ (e->globalPos() - _dragStartMousePosition));
		}
	}
}

void SeparatePanel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && _dragging) {
		_dragging = false;
	}
}

void SeparatePanel::leaveEventHook(QEvent *e) {
	Tooltip::Hide();
}

void SeparatePanel::leaveToChildEvent(QEvent *e, QWidget *child) {
	Tooltip::Hide();
}

} // namespace Ui
