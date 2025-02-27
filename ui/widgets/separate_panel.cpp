// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/separate_panel.h"

#include "ui/effects/ripple_animation.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/platform/ui_platform_window.h"
#include "ui/layers/box_content.h"
#include "ui/layers/layer_widget.h"
#include "ui/layers/show.h"
#include "ui/style/style_core_palette.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/qt_object_factory.h"
#include "ui/qt_weak_factory.h"
#include "ui/ui_utility.h"
#include "base/platform/base_platform_info.h"
#include "base/debug_log.h"
#include "base/invoke_queued.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

void OverlayWidgetCache(QPainter &p, Ui::RpWidget *widget) {
	if (widget) {
		widget->show();
		p.drawPixmap(widget->pos(), GrabWidget(widget));
		widget->hide();
	}
}

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

class SeparatePanel::FullScreenButton : public RippleButton {
public:
	FullScreenButton(QWidget *parent, const style::IconButton &st);

	void init(not_null<QWidget*> parentWindow);

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::IconButton &_st;

};

class SeparatePanel::ResizeEdge final : public RpWidget {
public:
	ResizeEdge(not_null<QWidget*> parent, Qt::Edges edges);

	void updateSize();
	void setParentPadding(QMargins padding);

private:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void updateFromResize(QPoint delta);

	const Qt::Edges _edges;
	QMargins _extent;
	QRect _startGeometry;
	QPoint _startPosition;
	bool _press = false;
	bool _resizing = false;

};

SeparatePanel::FullScreenButton::FullScreenButton(
	QWidget *parent,
	const style::IconButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
}

void SeparatePanel::FullScreenButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(st::radialBg);
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect());

	paintRipple(p, _st.rippleAreaPosition);

	const auto icon = &_st.icon;
	auto position = _st.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - icon->width()) / 2);
	}
	if (position.y() < 0) {
		position.setY((height() - icon->height()) / 2);
	}
	icon->paint(p, position, width());
}

QPoint SeparatePanel::FullScreenButton::prepareRippleStartPosition() const {
	auto result = mapFromGlobal(QCursor::pos())
		- _st.rippleAreaPosition;
	auto rect = QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize);
	return rect.contains(result)
		? result
		: DisabledRippleStartPosition();
}

QImage SeparatePanel::FullScreenButton::prepareRippleMask() const {
	return RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

SeparatePanel::ResizeEdge::ResizeEdge(
	not_null<QWidget*> parent,
	Qt::Edges edges)
: RpWidget(parent)
, _edges(edges) {
	show();
	setCursor([&] {
		if ((_edges == (Qt::LeftEdge | Qt::TopEdge))
			|| (_edges == (Qt::RightEdge | Qt::BottomEdge))) {
			return Qt::SizeFDiagCursor;
		} else if (_edges == Qt::TopEdge || _edges == Qt::BottomEdge) {
			return Qt::SizeVerCursor;
		} else if ((_edges == (Qt::RightEdge | Qt::TopEdge))
			|| (_edges == (Qt::LeftEdge | Qt::BottomEdge))) {
			return Qt::SizeBDiagCursor;
		} else if (_edges == Qt::RightEdge || _edges == Qt::LeftEdge) {
			return Qt::SizeHorCursor;
		} else {
			Unexpected("Bad edges in SeparatePanel::ResizeEdge.");
		}
	}());
}

void SeparatePanel::ResizeEdge::updateSize() {
	const auto parent = parentWidget()->rect();
	if ((_extent.left() + _extent.right() >= parent.width())
		|| (_extent.top() + _extent.bottom() >= parent.height())) {
		return;
	}
	if (_edges == (Qt::LeftEdge | Qt::TopEdge)) {
		setGeometry(0, 0, _extent.left(), _extent.top());
	} else if (_edges == Qt::TopEdge) {
		setGeometry(
			_extent.left(),
			0,
			parent.width() - _extent.left() - _extent.right(),
			_extent.top());
	} else if (_edges == (Qt::RightEdge | Qt::TopEdge)) {
		setGeometry(
			parent.width() - _extent.right(),
			0,
			_extent.right(),
			_extent.top());
	} else if (_edges == Qt::RightEdge) {
		setGeometry(
			parent.width() - _extent.right(),
			_extent.top(),
			_extent.right(),
			parent.height() - _extent.top() - _extent.bottom());
	} else if (_edges == (Qt::RightEdge | Qt::BottomEdge)) {
		setGeometry(
			parent.width() - _extent.right(),
			parent.height() - _extent.bottom(),
			_extent.right(),
			_extent.bottom());
	} else if (_edges == Qt::BottomEdge) {
		setGeometry(
			_extent.left(),
			parent.height() - _extent.bottom(),
			parent.width() - _extent.left() - _extent.right(),
			_extent.bottom());
	} else if (_edges == (Qt::LeftEdge | Qt::BottomEdge)) {
		setGeometry(
			0,
			parent.height() - _extent.bottom(),
			_extent.left(),
			_extent.bottom());
	} else if (_edges == Qt::LeftEdge) {
		setGeometry(
			0,
			_extent.top(),
			_extent.left(),
			parent.height() - _extent.top() - _extent.bottom());
	} else {
		Unexpected("Corrupt edges in SeparatePanel::ResizeEdge.");
	}
}

void SeparatePanel::ResizeEdge::setParentPadding(QMargins padding) {
	if (_extent != padding) {
		_extent = padding;
		updateSize();
	}
}

void SeparatePanel::ResizeEdge::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_press = true;
		_startPosition = e->globalPos();
		_startGeometry = window()->geometry();
	}
}

void SeparatePanel::ResizeEdge::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_press = false;
		_resizing = false;
	}
}

void SeparatePanel::ResizeEdge::mouseMoveEvent(QMouseEvent *e) {
	if (base::take(_press)) {
		if (const auto handle = window()->windowHandle()) {
			if (handle->startSystemResize(_edges)) {
				SendSynteticMouseEvent(
					this,
					QEvent::MouseButtonRelease,
					Qt::LeftButton);
			} else {
				_resizing = true;
			}
		}
	}
	if (_resizing) {
		updateFromResize(e->globalPos() - _startPosition);
	}
}

void SeparatePanel::ResizeEdge::updateFromResize(QPoint delta) {
	auto geometry = _startGeometry;
	const auto min = window()->minimumSize();
	const auto minw = std::max(min.width(), 80);
	const auto minh = std::max(min.height(), 40);
	const auto updateLeft = [&](int left) {
		geometry.setX(std::min(
			left,
			geometry.x() + geometry.width() - minw));
	};
	const auto updateRight = [&](int right) {
		geometry.setWidth(std::max(right - geometry.x(), minw));
	};
	const auto updateTop = [&](int top) {
		geometry.setY(std::min(
			top,
			geometry.y() + geometry.height() - minh));
	};
	const auto updateBottom = [&](int bottom) {
		geometry.setHeight(std::max(bottom - geometry.y(), minh));
	};
	if (_edges & Qt::LeftEdge) {
		updateLeft(geometry.x() + delta.x());
	} else if (_edges & Qt::RightEdge) {
		updateRight(geometry.x() + geometry.width() + delta.x());
	}
	if (_edges & Qt::TopEdge) {
		updateTop(geometry.y() + delta.y());
	} else if (_edges & Qt::BottomEdge) {
		updateBottom(geometry.y() + geometry.height() + delta.y());
	}
	window()->setGeometry(geometry);
}

SeparatePanel::SeparatePanel(SeparatePanelArgs &&args)
: RpWidget(args.parent)
, _menuSt(args.menuSt ? *args.menuSt : st::popupMenuWithIcons)
, _close(this, st::separatePanelClose)
, _back(this, object_ptr<IconButton>(this, st::separatePanelBack))
, _body(this)
, _titleHeight(st::separatePanelTitleHeight) {
	setMouseTracking(true);
	setWindowIcon(QGuiApplication::windowIcon());
	initControls();
	initLayout(args);

	rpl::combine(
		shownValue(),
		_fullscreen.value()
	) | rpl::filter([=](bool shown, bool) {
		return shown;
	}) | rpl::start_with_next([=](bool, bool fullscreen) {
		updateControlsVisibility(fullscreen);
		Platform::SetWindowMargins(
			this,
			_useTransparency ? computePadding() : QMargins());
	}, lifetime());

	Platform::FullScreenEvents(
		this
	) | rpl::start_with_next([=](Platform::FullScreenEvent event) {
		if (event == Platform::FullScreenEvent::DidEnter) {
			createFullScreenButtons();
		} else if (event == Platform::FullScreenEvent::WillExit) {
			_fullscreen = false;
		}
	}, lifetime());
}

SeparatePanel::~SeparatePanel() = default;

void SeparatePanel::setTitle(rpl::producer<QString> title) {
	_title.create(this, std::move(title), st::separatePanelTitle);
	updateTitleColors();
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_title->setVisible(!_fullscreen.current());
	updateTitleGeometry(width());
}

void SeparatePanel::setTitleHeight(int height) {
	_titleHeight = height;
	updateControlsGeometry();
}

void SeparatePanel::setTitleBadge(object_ptr<RpWidget> badge) {
	if (badge) {
		badge->setParent(this);
	}
	_titleBadge = std::move(badge);
	updateTitleGeometry(width());
}

void SeparatePanel::initControls() {
	_back->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_titleLeft.start(
			[=] { updateTitleGeometry(width()); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _back->lifetime());
	_back->hide(anim::type::instant);
	if (_fsBack) {
		_fsBack->hide(anim::type::instant);
	}
	_titleLeft.stop();

	_fullscreen.value(
	) | rpl::start_with_next([=](bool fullscreen) {
		if (!fullscreen) {
			_fsClose = nullptr;
			_fsMenuToggle = nullptr;
			_fsBack = nullptr;
		} else if (!_fsClose) {
			createFullScreenButtons();
		}
	}, lifetime());

	rpl::combine(
		widthValue(),
		_fullscreen.value()
	) | rpl::start_with_next([=](int width, bool fullscreen) {
		const auto padding = computePadding();
		_back->moveToLeft(padding.left(), padding.top());
		_close->moveToRight(padding.right(), padding.top());
		updateTitleGeometry(width);
	}, lifetime());

	_back->raise();
	_close->raise();
}

void SeparatePanel::createFullScreenButtons() {
	_fsClose = std::make_unique<FullScreenButton>(
		this,
		st::fullScreenPanelClose);
	initFullScreenButton(_fsClose.get());
	_fsClose->clicks() | rpl::to_empty | rpl::start_to_stream(
		_userCloseRequests,
		_fsClose->lifetime());

	_fsBack = std::make_unique<FadeWrapScaled<FullScreenButton>>(
		this,
		object_ptr<FullScreenButton>(this, st::fullScreenPanelBack));
	initFullScreenButton(_fsBack.get());
	_fsBack->toggle(_back->toggled(), anim::type::instant);
	if (_back->toggled()) {
		_fsBack->raise();
	}
	_fsBack->entity()->clicks() | rpl::to_empty | rpl::start_to_stream(
		_synteticBackRequests,
		_fsBack->lifetime());
	if (_menuToggle) {
		_fsMenuToggle = std::make_unique<FullScreenButton>(
			this,
			st::fullScreenPanelMenu);
		initFullScreenButton(_fsMenuToggle.get());
		if (const auto onstack = _menuToggleCreated) {
			onstack(_fsMenuToggle.get(), true);
		}
		_fsMenuToggle->setClickedCallback([=] {
			_menuToggle->clicked(
				_fsMenuToggle->clickModifiers(),
				Qt::LeftButton);
		});
	} else {
		_fsMenuToggle = nullptr;
	}
	geometryValue() | rpl::start_with_next([=](QRect geometry) {
		if (_fsAllowChildControls) {
			geometry = QRect(QPoint(), size());
		}
		const auto shift = st::separatePanelClose.rippleAreaPosition;
		_fsBack->move(geometry.topLeft() + shift);
		_fsBack->resize(st::fullScreenPanelBack.width, st::fullScreenPanelBack.height);
		_fsClose->move(geometry.topLeft() + QPoint(geometry.width() - _fsClose->width() - shift.x(), shift.y()));
		_fsClose->resize(st::fullScreenPanelClose.width, st::fullScreenPanelClose.height);
		if (_fsMenuToggle) {
			_fsMenuToggle->move(_fsClose->pos()
				- QPoint(_fsMenuToggle->width() + shift.x(), 0));
			_fsMenuToggle->resize(st::fullScreenPanelMenu.width, st::fullScreenPanelMenu.height);
		}
	}, _fsClose->lifetime());
}

void SeparatePanel::initFullScreenButton(not_null<QWidget*> button) {
	if (_fsAllowChildControls) {
		button->show();
		return;
	}
	button->setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::BypassWindowManagerHint
		| Qt::NoDropShadowWindowHint
		| Qt::Tool);
	button->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	button->setAttribute(Qt::WA_OpaquePaintEvent, false);
	button->setAttribute(Qt::WA_TranslucentBackground, true);
	button->setAttribute(Qt::WA_NoSystemBackground, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	button->setScreen(screen());
#else // Qt >= 6.0.0
	button->createWinId();
	button->windowHandle()->setScreen(windowHandle()->screen());
#endif
	button->show();
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

void SeparatePanel::overrideBodyColor(std::optional<QColor> color) {
	if (_bodyOverrideColor == color) {
		return;
	}
	_bodyOverrideColor = color;
	_bodyOverrideBorderParts = _bodyOverrideColor
		? createBorderImage(*_bodyOverrideColor)
		: QPixmap();
	update();
}

void SeparatePanel::overrideBottomBarColor(std::optional<QColor> color) {
	if (_bottomBarOverrideColor == color) {
		return;
	}
	_bottomBarOverrideColor = color;
	_bottomBarOverrideBorderParts = _bottomBarOverrideColor
		? createBorderImage(*_bottomBarOverrideColor)
		: QPixmap();
	update();
}

void SeparatePanel::setBottomBarHeight(int height) {
	Expects(!height || height >= st::callRadius);

	if (_bottomBarHeight == height) {
		return;
	}
	_bottomBarHeight = height;
	update();
}

style::palette *SeparatePanel::titleOverridePalette() const {
	return _titleOverridePalette.get();
}

void SeparatePanel::updateTitleGeometry(int newWidth) const {
	if (!_title && !_searchWrap) {
		return;
	}
	const auto progress = _titleLeft.value(_back->toggled() ? 1. : 0.);
	const auto left = anim::interpolate(
		st::separatePanelTitleLeft,
		_back->width() + st::separatePanelTitleSkip,
		progress);
	const auto padding = computePadding();
	const auto available = newWidth
		- rect::m::sum::h(padding)
		- left
		- _close->width();
	if (_title) {
		_title->resizeToWidth(
			std::min(
				available
					- (_menuToggle ? _menuToggle->width() : 0)
					- (_searchToggle ? _searchToggle->width() : 0)
					- (_titleBadge
						? (_titleBadge->width()
							+ st::separatePanelTitleBadgeSkip * 2)
						: 0),
				_title->textMaxWidth()));
		_title->moveToLeft(
			padding.left() + left,
			padding.top() + st::separatePanelTitleTop);
		if (_titleBadge) {
			_titleBadge->moveToLeft(
				rect::right(_title) + st::separatePanelTitleBadgeSkip,
				_title->y() + st::separatePanelTitleBadgeTop);
		}
	}
	if (_searchWrap) {
		_searchWrap->entity()->resize(available, _close->height());
		_searchWrap->move(padding.left() + left, padding.top());
		if (_searchField) {
			_searchField->resizeToWidth(available);
			_searchField->move(
				0,
				(_close->height() - _searchField->height()) / 2);
		}
	}
}

rpl::producer<> SeparatePanel::allBackRequests() const {
	return rpl::merge(
		_back->entity()->clicks() | rpl::to_empty,
		_synteticBackRequests.events());
}

rpl::producer<> SeparatePanel::backRequests() const {
	return allBackRequests(
	) | rpl::filter([=] {
		return !_searchField;
	});
}

rpl::producer<> SeparatePanel::allCloseRequests() const {
	return rpl::merge(
		_close->clicks() | rpl::to_empty,
		_userCloseRequests.events());
}

rpl::producer<> SeparatePanel::closeRequests() const {
	return allCloseRequests(
	) | rpl::filter([=] {
		return !_searchField;
	});
}

rpl::producer<> SeparatePanel::closeEvents() const {
	return _closeEvents.events();
}

void SeparatePanel::setBackAllowed(bool allowed) {
	_backAllowed = allowed;
	updateBackToggled();
}

void SeparatePanel::updateBackToggled() {
	const auto toggled = _backAllowed || (_searchField != nullptr);
	if (_back->toggled() != toggled) {
		_back->toggle(toggled, anim::type::normal);
		if (_fsBack) {
			_fsBack->toggle(toggled, anim::type::normal);
			if (toggled) {
				_fsBack->raise();
			}
		}
	}
}

void SeparatePanel::setMenuAllowed(
		Fn<void(const Menu::MenuCallback&)> fill,
		Fn<void(not_null<RpWidget*>, bool fullscreen)> created) {
	_menuToggle.create(this, st::separatePanelMenu);
	updateTitleButtonColors(_menuToggle.data());
	_menuToggle->show();
	_menuToggle->setClickedCallback([=] { showMenu(fill); });
	rpl::combine(
		widthValue(),
		_fullscreen.value()
	) | rpl::start_with_next([=](int width, bool) {
		const auto padding = computePadding();
		_menuToggle->moveToRight(
			padding.right() + _close->width(),
			padding.top());
	}, _menuToggle->lifetime());
	updateTitleGeometry(width());
	if (_fullscreen.current()) {
		createFullScreenButtons();
	}
	_menuToggleCreated = std::move(created);
	if (const auto onstack = _menuToggleCreated) {
		onstack(_menuToggle.data(), false);
	}
	if (!_animationCache.isNull()) {
		const auto rect = _menuToggle->geometry()
			| (_title ? _title->geometry() : QRect())
			| (_titleBadge ? _titleBadge->geometry() : QRect());
		auto p = QPainter(&_animationCache);
		p.fillRect(rect, st::windowBg);
		OverlayWidgetCache(p, _title);
		OverlayWidgetCache(p, _titleBadge);
		OverlayWidgetCache(p, _menuToggle);
	}
}

void SeparatePanel::setSearchAllowed(
		rpl::producer<QString> placeholder,
		Fn<void(std::optional<QString>)> queryChanged) {
	_searchPlaceholder = std::move(placeholder);
	_searchQueryChanged = std::move(queryChanged);
	_searchToggle.create(
		this,
		object_ptr<IconButton>(this, st::separatePanelSearch));
	const auto button = _searchToggle->entity();
	updateTitleButtonColors(button);
	_searchToggle->show(anim::type::instant);
	button->setClickedCallback([=] { toggleSearch(true); });

	rpl::combine(
		widthValue(),
		_fullscreen.value()
	) | rpl::start_with_next([=](int width, bool) {
		const auto padding = computePadding();
		_searchToggle->moveToRight(
			padding.right() + _close->width(),
			padding.top());
	}, _searchToggle->lifetime());
	updateTitleGeometry(width());
}

bool SeparatePanel::closeSearch() {
	if (!_searchField) {
		return false;
	}
	toggleSearch(false);
	return true;
}

void SeparatePanel::toggleSearch(bool shown) {
	const auto weak = Ui::MakeWeak(this);
	if (shown) {
		if (_searchWrap && _searchWrap->toggled()) {
			return;
		}
		_searchWrap.create(this, object_ptr<RpWidget>(this));
		const auto inner = _searchWrap->entity();
		inner->paintRequest() | rpl::start_with_next([=](QRect clip) {
			QPainter(inner).fillRect(clip, st::windowBg);
		}, inner->lifetime());
		_searchField = CreateChild<InputField>(
			inner,
			st::defaultMultiSelectSearchField,
			InputField::Mode::SingleLine,
			_searchPlaceholder.value());
		_searchField->show();
		_searchField->setFocusFast();

		const auto field = _searchField;
		field->changes() | rpl::filter([=] {
			return (_searchField == field);
		}) | rpl::start_with_next([=] {
			if (const auto onstack = _searchQueryChanged) {
				onstack(field->getLastText());
			}
		}, field->lifetime());

		rpl::merge(
			allBackRequests(),
			allCloseRequests()
		) | rpl::filter([=] {
			return (_searchField == field);
		}) | rpl::start_with_next([=] {
			toggleSearch(false);
		}, field->lifetime());

		if (const auto onstack = _searchQueryChanged) {
			onstack(QString());
			if (!weak) {
				return;
			}
		}

		updateTitleGeometry(width());
		_searchWrap->show(anim::type::normal);
		updateBackToggled();

		inner->shownValue(
		) | rpl::filter([=](bool active) {
			return active && (_searchField == field);
		}) | rpl::take(1) | rpl::start_with_next([=] {
			InvokeQueued(field, [=] {
				if (_searchField == field && window()->isActiveWindow()) {
					// In case focus is somewhat in a native child window,
					// like a webview, Qt glitches here with field showing
					// focused state, but not receiving any keyboard input:
					//
					// window()->windowHandle()->isActive() == false.
					//
					// Steps were: SeparatePanel with a WebView2 child,
					// some interaction with mouse inside the WebView2,
					// so that WebView2 gets focus and active window state,
					// then we call setSearchAllowed() and after animation
					// is finished try typing -> nothing happens.
					//
					// With this workaround it works fine.
					activateWindow();
				}
			});
		}, inner->lifetime());

		_searchWrap->shownValue(
		) | rpl::filter(
			!rpl::mappers::_1
		) | rpl::start_with_next([=] {
			_searchWrap.destroy();
		}, _searchWrap->lifetime());
	} else if (_searchField) {
		_searchField = nullptr;
		if (const auto onstack = _searchQueryChanged) {
			onstack(std::nullopt);
			if (!weak) {
				return;
			}
		}

		_searchWrap->hide(anim::type::normal);
		updateBackToggled();
	}
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
				- computePadding().right()
				- _close->width()
				+ st::separatePanelMenuPosition.x()),
			st::separatePanelMenuPosition.y())));
	}
}

bool SeparatePanel::createMenu(not_null<IconButton*> button) {
	if (_menu) {
		return false;
	}
	_menu = base::make_unique_q<PopupMenu>(this, _menuSt);
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
			const auto searchQuery = _searchField
				? _searchField->getLastText().trimmed()
				: QString();
			if (!searchQuery.isEmpty()) {
				_searchField->clear();
				_searchField->setFocus();
			} else if (_back->toggled()) {
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
	updateControlsVisibility(_fullscreen.current());
}

void SeparatePanel::updateControlsVisibility(bool fullscreen) {
	if (_title) {
		_title->setVisible(!fullscreen);
	}
	if (_titleBadge) {
		_titleBadge->setVisible(!fullscreen);
	}
	_close->setVisible(!fullscreen);
	if (_menuToggle) {
		_menuToggle->setVisible(!fullscreen);
	}
	if (fullscreen) {
		_back->lower();
	} else {
		_back->raise();
	}
	if (!_back->toggled()) {
		_back->setVisible(false);
		if (_fsBack) {
			_fsBack->setVisible(false);
		}
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

void SeparatePanel::setInnerSize(QSize size, bool allowResize) {
	Expects(!size.isEmpty());

	if (_allowResize != allowResize) {
		_allowResize = allowResize;
		if (!_allowResize) {
			_resizeEdges.clear();
		} else if (_resizeEdges.empty()) {
			const auto areas = std::array<Qt::Edges, 8>{ {
				Qt::LeftEdge | Qt::TopEdge,
				Qt::TopEdge,
				Qt::RightEdge | Qt::TopEdge,
				Qt::RightEdge,
				Qt::RightEdge | Qt::BottomEdge,
				Qt::BottomEdge,
				Qt::LeftEdge | Qt::BottomEdge,
				Qt::LeftEdge
			} };
			for (const auto area : areas) {
				_resizeEdges.push_back(
					std::make_unique<ResizeEdge>(this, area));
				_resizeEdges.back()->showOn(
					_fullscreen.value() | rpl::map(!rpl::mappers::_1));
			}
		}
	}
	if (rect().isEmpty()) {
		initGeometry(size);
	} else {
		updateGeometry(size);
	}
}

QRect SeparatePanel::innerGeometry() const {
	return _body->geometry();
}

void SeparatePanel::toggleFullScreen(bool fullscreen) {
	_fullscreen = fullscreen;
	if (fullscreen) {
		showFullScreen();
	} else {
		showNormal();
	}
}

void SeparatePanel::allowChildFullScreenControls(bool allow) {
	if (_fsAllowChildControls == allow) {
		return;
	}
	_fsAllowChildControls = allow;
	if (_fullscreen.current()) {
		createFullScreenButtons();
	}
}

rpl::producer<bool> SeparatePanel::fullScreenValue() const {
	return _fullscreen.value();
}

QMargins SeparatePanel::computePadding() const {
	return _fullscreen.current() ? QMargins() : _padding;
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
	for (const auto &edge : _resizeEdges) {
		edge->setParentPadding(_padding);
	}

	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	if (!_fullscreen.current()) {
		const auto rect = [&] {
			const auto initRect = QRect(QPoint(), size);
			const auto shift = center - initRect.center();
			return initRect.translated(shift).marginsAdded(_padding);
		}();
		move(rect.topLeft());
		if (_allowResize) {
			setMinimumSize(rect.size());
		} else {
			setFixedSize(rect.size());
		}
		updateControlsGeometry();
	}
}

void SeparatePanel::updateGeometry(QSize size) {
	if (!_fullscreen.current()) {
		size = QRect(QPoint(), size).marginsAdded(_padding).size();
		if (_allowResize) {
			setMinimumSize(size);
		} else {
			setFixedSize(size);
		}
		updateControlsGeometry();
	}
	update();
}

void SeparatePanel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
	for (const auto &edge : _resizeEdges) {
		edge->updateSize();
	}
}

void SeparatePanel::updateControlsGeometry() {
	const auto padding = computePadding();
	const auto top = padding.top()
		+ (_fullscreen.current() ? 0 : _titleHeight);
	_body->setGeometry(
		padding.left(),
		top,
		width() - padding.left() - padding.right(),
		height() - top - padding.bottom());
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
	if (_useTransparency && !_fullscreen.current()) {
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

	const auto &header = (_titleHeight
		&& !_fullscreen.current()
		&& _titleOverrideColor)
		? _titleOverrideBorderParts
		: _bodyOverrideColor
		? _bodyOverrideBorderParts
		: _borderParts;
	const auto &footer = (_bottomBarHeight && _bottomBarOverrideColor)
		? _bottomBarOverrideBorderParts
		: _bodyOverrideColor
		? _bodyOverrideBorderParts
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
		footer,
		bottomleft);

	const auto bottomright = QRect(QPoint(part2, part2) * factor, corner);
	p.drawPixmap(
		QRect(width() - part1, height() - part1, part1, part1),
		footer,
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
		footer,
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
	fillLeft(part1, height() - part1, _borderParts);
	fillRight(part1, height() - part1, _borderParts);
	paintBodyBg(p, radius);
}

void SeparatePanel::paintBodyBg(QPainter &p, int radius) const {
	const auto padding = computePadding();
	const auto fillBody = [&](int from, int till, QColor color) {
		if (till <= from) {
			return;
		}
		p.fillRect(
			padding.left(),
			from,
			width() - padding.left() - padding.right(),
			till - from,
			color);
	};
	const auto bg = _bodyOverrideColor.value_or(st::windowBg->c);
	const auto chosenFooter = (_bottomBarHeight && _bottomBarOverrideColor)
		? _bottomBarOverrideColor
		: _bodyOverrideColor;
	const auto footerColor = chosenFooter.value_or(st::windowBg->c);
	const auto chosenHeader = (_titleHeight
		&& !_fullscreen.current()
		&& _titleOverrideColor)
		? _titleOverrideColor
		: _bodyOverrideColor;
	const auto titleColor = chosenHeader.value_or(st::windowBg->c);
	const auto niceOverscroll = !_layer && ::Platform::IsMac();
	if ((niceOverscroll && titleColor == footerColor)
		|| (titleColor == footerColor && titleColor == bg)) {
		fillBody(
			padding.top() + radius,
			height() - padding.bottom() - radius,
			titleColor);
	} else if (niceOverscroll || titleColor == bg || footerColor == bg) {
		const auto top = niceOverscroll
			? (height() / 2)
			: (titleColor != bg)
			? (padding.top() + _titleHeight)
			: (height() - padding.bottom() - _bottomBarHeight);
		fillBody(padding.top() + radius, top, titleColor);
		fillBody(top, height() - padding.bottom() - radius, footerColor);
	} else {
		const auto one = padding.top() + _titleHeight;
		const auto two = height() - padding.bottom() - _bottomBarHeight;
		fillBody(padding.top() + radius, one, titleColor);
		fillBody(one, two, bg);
		fillBody(two, height() - padding.bottom() - radius, footerColor);
	}
}

void SeparatePanel::paintOpaqueBorder(QPainter &p) const {
	const auto border = st::windowShadowFgFallback;
	const auto padding = computePadding();
	if (!_fullscreen.current()) {
		p.fillRect(0, 0, width(), padding.top(), border);
		p.fillRect(
			myrtlrect(
				0,
				padding.top(),
				padding.left(),
				height() - padding.top()),
			border);
		p.fillRect(
			myrtlrect(
				width() - padding.right(),
				padding.top(),
				padding.right(),
				height() - padding.top()),
			border);
		p.fillRect(
			padding.left(),
			height() - padding.bottom(),
			width() - padding.left() - padding.right(),
			padding.bottom(),
			border);
	}
	paintBodyBg(p);
}

void SeparatePanel::closeEvent(QCloseEvent *e) {
	e->ignore();
	_userCloseRequests.fire({});
}

void SeparatePanel::mousePressEvent(QMouseEvent *e) {
	if (_fullscreen.current()) {
		return;
	}
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
	if (_fullscreen.current()) {
		return;
	} else if (_dragging) {
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition
				+ (e->globalPos() - _dragStartMousePosition));
		}
	}
}

void SeparatePanel::mouseReleaseEvent(QMouseEvent *e) {
	if (_fullscreen.current()) {
		return;
	} else if (e->button() == Qt::LeftButton && _dragging) {
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
