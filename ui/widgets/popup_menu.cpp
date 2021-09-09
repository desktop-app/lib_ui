// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/popup_menu.h"

#include "ui/image/image_prepare.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/ui_utility.h"
#include "ui/delayed_activation.h"
#include "ui/painter.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_adapters.h"

#include <QtGui/QtEvents>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kShadowCornerMultiplier = 3;

[[nodiscard]] not_null<QImage*> PrepareCachedShadow(
		style::margins padding,
		not_null<const style::Shadow*> shadow,
		not_null<const RoundRect*> body,
		rpl::lifetime &lifetime) {
	const auto radius = st::roundRadiusSmall;
	const auto side = radius * kShadowCornerMultiplier;
	const auto middle = radius;
	const auto size = side * 2 + middle;
	const auto rect = QRect(0, 0, size, size);
	const auto result = lifetime.make_state<QImage>(
		rect.marginsAdded(padding).size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result->setDevicePixelRatio(style::DevicePixelRatio());
	const auto render = [=] {
		result->fill(Qt::transparent);
		auto p = QPainter(result);
		const auto inner = QRect(padding.left(), padding.top(), size, size);
		const auto outerWidth = padding.left() + size + padding.right();
		Shadow::paint(p, inner, outerWidth, *shadow);
		p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		body->paint(p, inner);
	};
	render();
	style::PaletteChanged(
	) | rpl::start_with_next(render, lifetime);
	return result;
}

void PaintCachedShadow(
		QPainter &p,
		QSize outer,
		style::margins padding,
		const QImage &cached) {
	const auto fill = [&](
			int dstx, int dsty, int dstw, int dsth,
			int srcx, int srcy, int srcw, int srch) {
		p.drawImage(
			QRect(dstx, dsty, dstw, dsth),
			cached,
			QRect(
				QPoint(srcx, srcy) * style::DevicePixelRatio(),
				QSize(srcw, srch) * style::DevicePixelRatio()));
	};
	const auto paintCorner = [&](
			int width, int height,
			int dstx, int dsty,
			int srcx, int srcy) {
		fill(dstx, dsty, width, height, srcx, srcy, width, height);
	};

	const auto radius = st::roundRadiusSmall;
	const auto side = radius * kShadowCornerMultiplier;
	const auto middle = radius;
	const auto size = side * 2 + middle;
	paintCorner( // Top-Left
		padding.left() + side,
		padding.top() + side,
		0,
		0,
		0,
		0);
	paintCorner( // Top-Right
		side + padding.right(),
		padding.top() + side,
		outer.width() - side - padding.right(),
		0,
		padding.left() + size - side,
		0);
	paintCorner( // Bottom-Right
		side + padding.right(),
		side + padding.bottom(),
		outer.width() - side - padding.right(),
		outer.height() - side - padding.bottom(),
		padding.left() + size - side,
		padding.top() + size - side);
	paintCorner( // Bottom-Left
		padding.left() + side,
		side + padding.bottom(),
		0,
		outer.height() - side - padding.bottom(),
		0,
		padding.top() + size - side);
	const auto fillx = outer.width()
		- padding.left()
		- padding.right()
		- 2 * side;
	fill( // Top
		padding.left() + side,
		0,
		fillx,
		padding.top(),
		padding.left() + side + (middle / 2),
		0,
		1,
		padding.top());
	fill( // Bottom
		padding.left() + side,
		outer.height() - padding.bottom(),
		fillx,
		padding.bottom(),
		padding.left() + side + (middle / 2),
		padding.top() + size,
		1,
		padding.bottom());
	const auto filly = outer.height()
		- padding.top()
		- padding.bottom()
		- 2 * side;
	fill( // Left
		0,
		padding.top() + side,
		padding.left(),
		filly,
		0,
		padding.top() + side + (middle / 2),
		padding.left(),
		1);
	fill( // Right
		outer.width() - padding.right(),
		padding.top() + side,
		padding.right(),
		filly,
		padding.left() + size,
		padding.top() + side + (middle / 2),
		padding.right(),
		1);
}

} // namespace

PopupMenu::PopupMenu(QWidget *parent, const style::PopupMenu &st)
: RpWidget(parent)
, _st(st)
, _roundRect(ImageRoundRadius::Small, _st.menu.itemBg)
, _scroll(this, st::defaultMultiSelect.scroll)
, _menu(_scroll->setOwnedWidget(
	object_ptr<PaddingWrap<Menu::Menu>>(
		_scroll.data(),
		object_ptr<Menu::Menu>(_scroll.data(), _st.menu),
		_st.scrollPadding))->entity()) {
	init();
}

PopupMenu::PopupMenu(QWidget *parent, QMenu *menu, const style::PopupMenu &st)
: RpWidget(parent)
, _st(st)
, _roundRect(ImageRoundRadius::Small, _st.menu.itemBg)
, _scroll(this, st::defaultMultiSelect.scroll)
, _menu(_scroll->setOwnedWidget(
	object_ptr<PaddingWrap<Menu::Menu>>(
		_scroll.data(),
		object_ptr<Menu::Menu>(_scroll.data(), menu, _st.menu),
		_st.scrollPadding))->entity()) {
	init();

	for (const auto &action : actions()) {
		if (const auto submenu = action->menu()) {
			_submenus.emplace(
				action,
				base::make_unique_q<PopupMenu>(parentWidget(), submenu, st)
			).first->second->deleteOnHide(false);
		}
	}
}

void PopupMenu::init() {
	using namespace rpl::mappers;

	Integration::Instance().forcePopupMenuHideRequests(
	) | rpl::start_with_next([=] {
		hideMenu(true);
	}, lifetime());

	const auto paddingWrap = static_cast<PaddingWrap<Menu::Menu>*>(
		_menu->parentWidget());
	paddingWrap->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		const auto top = clip.intersected(
			QRect(0, 0, paddingWrap->width(), _st.scrollPadding.top()));
		const auto bottom = clip.intersected(QRect(
			0,
			paddingWrap->height() - _st.scrollPadding.bottom(),
			paddingWrap->width(),
			_st.scrollPadding.bottom()));
		auto p = QPainter(paddingWrap);
		if (!top.isEmpty()) {
			p.fillRect(top, _st.menu.itemBg);
		}
		if (!bottom.isEmpty()) {
			p.fillRect(bottom, _st.menu.itemBg);
		}
	}, paddingWrap->lifetime());

	_menu->scrollToRequests(
	) | rpl::start_with_next([=](ScrollToRequest request) {
		_scroll->scrollTo({
			request.ymin ? (_st.scrollPadding.top() + request.ymin) : 0,
			(request.ymax == _menu->height()
				? paddingWrap->height()
				: (_st.scrollPadding.top() + request.ymax)),
		});
	}, _menu->lifetime());

	_menu->resizesFromInner(
	) | rpl::start_with_next([=] {
		handleMenuResize();
	}, _menu->lifetime());
	_menu->setActivatedCallback([this](const Menu::CallbackData &data) {
		handleActivated(data);
	});
	_menu->setTriggeredCallback([this](const Menu::CallbackData &data) {
		handleTriggered(data);
	});
	_menu->setKeyPressDelegate([this](int key) { return handleKeyPress(key); });
	_menu->setMouseMoveDelegate([this](QPoint globalPosition) { handleMouseMove(globalPosition); });
	_menu->setMousePressDelegate([this](QPoint globalPosition) { handleMousePress(globalPosition); });
	_menu->setMouseReleaseDelegate([this](QPoint globalPosition) { handleMouseRelease(globalPosition); });

	handleCompositingUpdate();

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::BypassWindowManagerHint | Qt::Popup | Qt::NoDropShadowWindowHint);
	setMouseTracking(true);

	hide();

	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
}

not_null<PopupMenu*> PopupMenu::ensureSubmenu(not_null<QAction*> action) {
	const auto &list = actions();
	const auto i = ranges::find(list, action);
	Assert(i != end(list));

	const auto j = _submenus.find(action);
	if (j != end(_submenus)) {
		return j->second.get();
	}
	const auto result = _submenus.emplace(
		action,
		base::make_unique_q<PopupMenu>(parentWidget(), st())
	).first->second.get();
	result->deleteOnHide(false);
	return result;
}

void PopupMenu::removeSubmenu(not_null<QAction*> action) {
	const auto menu = _submenus.take(action);
	if (menu && menu->get() == _activeSubmenu) {
		base::take(_activeSubmenu)->hideMenu(true);
	}
}

void PopupMenu::checkSubmenuShow() {
	if (_activeSubmenu) {
		return;
	} else if (const auto item = _menu->findSelectedAction()) {
		if (item->lastTriggeredSource() == Menu::TriggeredSource::Mouse) {
			if (_submenus.contains(item->action())) {
				item->setClicked(Menu::TriggeredSource::Mouse);
			}
		}
	}
}

void PopupMenu::handleCompositingUpdate() {
	const auto line = st::lineWidth;
	_padding = _useTransparency
		? _st.shadow.extend
		: style::margins(line, line, line, line);
	_scroll->moveToLeft(_padding.left(), _padding.top());
	handleMenuResize();
	updateRoundingOverlay();
}

void PopupMenu::updateRoundingOverlay() {
	if (!_useTransparency) {
		_roundingOverlay.destroy();
		return;
	} else if (_roundingOverlay) {
		return;
	}
	_roundingOverlay.create(this);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_roundingOverlay->setGeometry(QRect(QPoint(), size));
	}, _roundingOverlay->lifetime());

	const auto shadow = PrepareCachedShadow(
		_padding,
		&_st.shadow,
		&_roundRect,
		_roundingOverlay->lifetime());

	_roundingOverlay->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_roundingOverlay.data());
		auto hq = PainterHighQualityEnabler(p);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		_roundRect.paint(p, _inner, RectPart::AllCorners);
		if (!_grabbingForPanelAnimation) {
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			PaintCachedShadow(p, size(), _padding, *shadow);
		}
	}, _roundingOverlay->lifetime());

	_roundingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void PopupMenu::handleMenuResize() {
	auto newWidth = _padding.left() + _st.scrollPadding.left() + _menu->width() + _st.scrollPadding.right() + _padding.right();
	auto newHeight = _padding.top() + _st.scrollPadding.top() + _menu->height() + _st.scrollPadding.bottom() + _padding.bottom();
	const auto wantedHeight = newHeight - _padding.top() - _padding.bottom();
	const auto scrollHeight = _st.maxHeight
		? std::min(_st.maxHeight, wantedHeight)
		: wantedHeight;
	_scroll->resize(
		newWidth - _padding.left() - _padding.right(),
		scrollHeight);
	resize(newWidth, _padding.top() + scrollHeight + _padding.bottom());
	_inner = rect().marginsRemoved(_padding);
}

not_null<QAction*> PopupMenu::addAction(
		base::unique_qptr<Menu::ItemBase> widget) {
	return _menu->addAction(std::move(widget));
}

not_null<QAction*> PopupMenu::addAction(const QString &text, Fn<void()> callback, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, std::move(callback), icon, iconOver);
}

not_null<QAction*> PopupMenu::addAction(
		const QString &text,
		std::unique_ptr<PopupMenu> submenu) {
	const auto action = _menu->addAction(text, std::make_unique<QMenu>());
	const auto saved = _submenus.emplace(
		action,
		base::unique_qptr<PopupMenu>(submenu.release())
	).first->second.get();
	saved->setParent(parentWidget());
	saved->deleteOnHide(false);
	return action;
}

not_null<QAction*> PopupMenu::addSeparator() {
	return _menu->addSeparator();
}

void PopupMenu::clearActions() {
	_submenus.clear();
	return _menu->clearActions();
}

const std::vector<not_null<QAction*>> &PopupMenu::actions() const {
	return _menu->actions();
}

bool PopupMenu::empty() const {
	return _menu->empty();
}

void PopupMenu::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (_a_show.animating()) {
		if (auto opacity = _a_opacity.value(_hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(p, 0, 0, width(), _a_show.value(1.), opacity);
		}
	} else if (_a_opacity.animating()) {
		p.setOpacity(_a_opacity.value(0.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else if (_showAnimation) {
		_showAnimation->paintFrame(p, 0, 0, width(), 1., 1.);
		_showAnimation.reset();
		PostponeCall(this, [=] { showChildren(); });
	} else {
		paintBg(p);
	}
}

void PopupMenu::paintBg(QPainter &p) {
	if (!_useTransparency) {
		p.fillRect(0, 0, width() - _padding.right(), _padding.top(), _st.shadow.fallback);
		p.fillRect(width() - _padding.right(), 0, _padding.right(), height() - _padding.bottom(), _st.shadow.fallback);
		p.fillRect(_padding.left(), height() - _padding.bottom(), width() - _padding.left(), _padding.bottom(), _st.shadow.fallback);
		p.fillRect(0, _padding.top(), _padding.left(), height() - _padding.top(), _st.shadow.fallback);
	}
}

void PopupMenu::handleActivated(const Menu::CallbackData &data) {
	if (data.source == TriggeredSource::Mouse) {
		if (!popupSubmenuFromAction(data)) {
			if (const auto currentSubmenu = base::take(_activeSubmenu)) {
				currentSubmenu->hideMenu(true);
			}
		}
	}
}

void PopupMenu::handleTriggered(const Menu::CallbackData &data) {
	if (!popupSubmenuFromAction(data)) {
		_triggering = true;
		hideMenu();
		data.action->trigger();
		_triggering = false;
		if (_deleteLater) {
			_deleteLater = false;
			deleteLater();
		}
	}
}

bool PopupMenu::popupSubmenuFromAction(const Menu::CallbackData &data) {
	if (!data.action) {
		return false;
	}
	if (const auto i = _submenus.find(data.action); i != end(_submenus)) {
		const auto submenu = i->second.get();
		if (_activeSubmenu != submenu) {
			popupSubmenu(data.action, submenu, data.actionTop, data.source);
		}
		return true;
	}
	return false;
}

void PopupMenu::popupSubmenu(
		not_null<QAction*> action,
		not_null<PopupMenu*> submenu,
		int actionTop,
		TriggeredSource source) {
	if (auto currentSubmenu = base::take(_activeSubmenu)) {
		currentSubmenu->hideMenu(true);
	}
	if (submenu) {
		QPoint p(_inner.x() + (style::RightToLeft() ? _padding.right() : _inner.width() - _padding.left()), _inner.y() + actionTop);
		_activeSubmenu = submenu;
		_activeSubmenu->showMenu(geometry().topLeft() + p, this, source);
		_menu->setChildShownAction(action);
	}
}

void PopupMenu::forwardKeyPress(not_null<QKeyEvent*> e) {
	if (!handleKeyPress(e->key())) {
		_menu->handleKeyPress(e);
	}
}

bool PopupMenu::handleKeyPress(int key) {
	if (_activeSubmenu) {
		_activeSubmenu->handleKeyPress(key);
		return true;
	} else if (key == Qt::Key_Escape) {
		hideMenu(_parent ? true : false);
		return true;
	} else if (key == (style::RightToLeft() ? Qt::Key_Right : Qt::Key_Left)) {
		if (_parent) {
			hideMenu(true);
			return true;
		}
	} else if (key == (style::RightToLeft() ? Qt::Key_Left : Qt::Key_Right)) {
		if (const auto item = _menu->findSelectedAction()) {
			if (_submenus.contains(item->action())) {
				item->setClicked(Menu::TriggeredSource::Keyboard);
			}
		}
	}
	return false;
}

void PopupMenu::handleMouseMove(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseMove(globalPosition);
	}
}

void PopupMenu::handleMousePress(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMousePress(globalPosition);
	} else {
		hideMenu();
	}
}

void PopupMenu::handleMouseRelease(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseRelease(globalPosition);
	} else {
		hideMenu();
	}
}

void PopupMenu::focusOutEvent(QFocusEvent *e) {
	hideMenu();
}

void PopupMenu::hideEvent(QHideEvent *e) {
	if (_deleteOnHide) {
		if (_triggering) {
			_deleteLater = true;
		} else {
			deleteLater();
		}
	}
}

void PopupMenu::keyPressEvent(QKeyEvent *e) {
	forwardKeyPress(e);
}

void PopupMenu::mouseMoveEvent(QMouseEvent *e) {
	forwardMouseMove(e->globalPos());
}

void PopupMenu::mousePressEvent(QMouseEvent *e) {
	forwardMousePress(e->globalPos());
}

void PopupMenu::hideMenu(bool fast) {
	if (isHidden()) {
		return;
	}
	if (_parent && !_a_opacity.animating()) {
		_parent->childHiding(this);
	}
	if (fast) {
		hideFast();
	} else {
		hideAnimated();
		if (_parent) {
			_parent->hideMenu();
		}
	}
	if (_activeSubmenu) {
		_activeSubmenu->hideMenu(fast);
	}
}

void PopupMenu::childHiding(PopupMenu *child) {
	if (_activeSubmenu && _activeSubmenu == child) {
		_activeSubmenu = nullptr;
	}
	if (!_activeSubmenu) {
		_menu->setChildShownAction(nullptr);
	}
	if (!_hiding && !isHidden()) {
		raise();
		activateWindow();
	}
}

void PopupMenu::setOrigin(PanelAnimation::Origin origin) {
	_origin = _forcedOrigin.value_or(origin);
}

void PopupMenu::setForcedOrigin(PanelAnimation::Origin origin) {
	_forcedOrigin = origin;
}

void PopupMenu::showAnimated(PanelAnimation::Origin origin) {
	setOrigin(origin);
	showStarted();
}

void PopupMenu::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	startOpacityAnimation(true);
}

void PopupMenu::hideFast() {
	if (isHidden()) return;

	_hiding = false;
	_a_opacity.stop();
	hideFinished();
}

void PopupMenu::hideFinished() {
	_a_show.stop();
	_cache = QPixmap();
	if (!isHidden()) {
		hide();
	}
}

void PopupMenu::prepareCache() {
	if (_a_opacity.animating()) return;

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	showChildren();
	_cache = GrabWidget(this);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
}

void PopupMenu::startOpacityAnimation(bool hiding) {
	_hiding = false;
	if (!_useTransparency) {
		_a_opacity.stop();
		if (hiding) {
			hideFinished();
		} else {
			update();
		}
		return;
	}
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., _st.duration);
}

void PopupMenu::showStarted() {
	if (isHidden()) {
		show();
		startShowAnimation();
		return;
	} else if (!_hiding) {
		return;
	}
	startOpacityAnimation(false);
}

void PopupMenu::startShowAnimation() {
	if (!_useTransparency) {
		_a_show.stop();
		update();
		return;
	}
	if (!_a_show.animating()) {
		auto opacityAnimation = base::take(_a_opacity);
		showChildren();
		auto cache = grabForPanelAnimation();
		_a_opacity = base::take(opacityAnimation);

		const auto pixelRatio = style::DevicePixelRatio();
		_showAnimation = std::make_unique<PanelAnimation>(_st.animation, _origin);
		_showAnimation->setFinalImage(std::move(cache), QRect(_inner.topLeft() * pixelRatio, _inner.size() * pixelRatio));
		if (_useTransparency) {
			_showAnimation->setCornerMasks(
				Images::CornersMask(ImageRoundRadius::Small));
		} else {
			_showAnimation->setSkipShadow(true);
		}
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { showAnimationCallback(); }, 0., 1., _st.showDuration);
}

void PopupMenu::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			_hiding = false;
			hideFinished();
		} else {
			showChildren();
		}
	}
}

void PopupMenu::showAnimationCallback() {
	update();
}

QImage PopupMenu::grabForPanelAnimation() {
	SendPendingMoveResizeEvents(this);
	const auto pixelRatio = style::DevicePixelRatio();
	auto result = QImage(size() * pixelRatio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(pixelRatio);
	result.fill(Qt::transparent);
	{
		QPainter p(&result);
		_grabbingForPanelAnimation = true;
		p.fillRect(_inner, _st.menu.itemBg);
		for (const auto child : children()) {
			if (const auto widget = qobject_cast<QWidget*>(child)) {
				RenderWidget(p, widget, widget->pos());
			}
		}
		_grabbingForPanelAnimation = false;
	}
	return result;
}

void PopupMenu::deleteOnHide(bool del) {
	_deleteOnHide = del;
}

void PopupMenu::popup(const QPoint &p) {
	showMenu(p, nullptr, TriggeredSource::Mouse);
}

void PopupMenu::showMenu(const QPoint &p, PopupMenu *parent, TriggeredSource source) {
	const auto screen = base::QScreenNearestTo(p);
	if (!screen
		|| (!parent && ::Platform::IsMac() && !Platform::IsApplicationActive())) {
		_hiding = false;
		_a_opacity.stop();
		_a_show.stop();
		_cache = QPixmap();
		hide();
		if (_deleteOnHide) {
			deleteLater();
		}
		return;
	}
	_parent = parent;

	using Origin = PanelAnimation::Origin;
	auto origin = Origin::TopLeft;
	const auto forceLeft = _forcedOrigin
		&& (*_forcedOrigin == Origin::TopLeft
			|| *_forcedOrigin == Origin::BottomLeft);
	const auto forceTop = _forcedOrigin
		&& (*_forcedOrigin == Origin::TopLeft
			|| *_forcedOrigin == Origin::TopRight);
	const auto forceRight = _forcedOrigin
		&& (*_forcedOrigin == Origin::TopRight
			|| *_forcedOrigin == Origin::BottomRight);
	const auto forceBottom = _forcedOrigin
		&& (*_forcedOrigin == Origin::BottomLeft
			|| *_forcedOrigin == Origin::BottomRight);
	auto w = p - QPoint(0, _padding.top());
	auto r = screen->availableGeometry();
	_useTransparency = Platform::TranslucentWindowsSupported(p);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	handleCompositingUpdate();
	if (style::RightToLeft()) {
		const auto badLeft = (w.x() - width() < r.x() - _padding.left());
		if (forceRight || (badLeft && !forceLeft)) {
			if (_parent && w.x() + _parent->width() - _padding.left() - _padding.right() + width() - _padding.right() <= r.x() + r.width()) {
				w.setX(w.x() + _parent->width() - _padding.left() - _padding.right());
			} else {
				w.setX(r.x() - _padding.left());
			}
		} else {
			w.setX(w.x() - width());
		}
	} else {
		const auto badLeft = (w.x() + width() - _padding.right() > r.x() + r.width());
		if (forceRight || (badLeft && !forceLeft)) {
			if (_parent && w.x() - _parent->width() + _padding.left() + _padding.right() - width() + _padding.right() >= r.x() - _padding.left()) {
				w.setX(w.x() + _padding.left() + _padding.right() - _parent->width() - width() + _padding.left() + _padding.right());
			} else {
				w.setX(p.x() - width() + _padding.right());
			}
			origin = PanelAnimation::Origin::TopRight;
		}
	}
	const auto badTop = (w.y() + height() - _padding.bottom() > r.y() + r.height());
	if (forceBottom || (badTop && !forceTop)) {
		if (_parent) {
			w.setY(r.y() + r.height() - height() + _padding.bottom());
		} else {
			w.setY(p.y() - height() + _padding.bottom());
			origin = (origin == PanelAnimation::Origin::TopRight) ? PanelAnimation::Origin::BottomRight : PanelAnimation::Origin::BottomLeft;
		}
	}
	if (w.x() < r.x()) {
		w.setX(r.x());
	}
	if (w.y() < r.y()) {
		w.setY(r.y());
	}
	move(w);

	setOrigin(origin);
	_menu->setShowSource(source);

	startShowAnimation();

	Platform::UpdateOverlayed(this);
	show();
	Platform::ShowOverAll(this);
	raise();
	activateWindow();
}

PopupMenu::~PopupMenu() {
	for (const auto &[action, submenu] : base::take(_submenus)) {
		delete submenu;
	}
	if (const auto parent = parentWidget()) {
		const auto focused = QApplication::focusWidget();
		if (_reactivateParent
			&& focused != nullptr
			&& Ui::InFocusChain(parent->window())) {
			ActivateWindowDelayed(parent);
		}
	}
	if (_destroyedCallback) {
		_destroyedCallback();
	}
}

} // namespace Ui
