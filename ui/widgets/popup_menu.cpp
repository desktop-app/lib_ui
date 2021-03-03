// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/popup_menu.h"

#include "ui/widgets/shadow.h"
#include "ui/image/image_prepare.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/ui_utility.h"
#include "ui/delayed_activation.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_adapters.h"

#include <QtGui/QtEvents>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Ui {

PopupMenu::PopupMenu(QWidget *parent, const style::PopupMenu &st)
: RpWidget(parent)
, _st(st)
, _roundRect(ImageRoundRadius::Small, _st.menu.itemBg)
, _menu(this, _st.menu) {
	init();
}

PopupMenu::PopupMenu(QWidget *parent, QMenu *menu, const style::PopupMenu &st)
: RpWidget(parent)
, _st(st)
, _roundRect(ImageRoundRadius::Small, _st.menu.itemBg)
, _menu(this, menu, _st.menu) {
	init();

	for (auto action : actions()) {
		if (auto submenu = action->menu()) {
			auto it = _submenus.insert(action, new PopupMenu(parentWidget(), submenu, st));
			it.value()->deleteOnHide(false);
		}
	}
}

void PopupMenu::init() {
	using namespace rpl::mappers;

	Integration::Instance().forcePopupMenuHideRequests(
	) | rpl::start_with_next([=] {
		hideMenu(true);
	}, lifetime());

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

void PopupMenu::handleCompositingUpdate() {
	_padding = _useTransparency ? _st.shadow.extend : style::margins(st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth);
	_menu->moveToLeft(_padding.left() + _st.scrollPadding.left(), _padding.top() + _st.scrollPadding.top());
	handleMenuResize();
}

void PopupMenu::handleMenuResize() {
	auto newWidth = _padding.left() + _st.scrollPadding.left() + _menu->width() + _st.scrollPadding.right() + _padding.right();
	auto newHeight = _padding.top() + _st.scrollPadding.top() + _menu->height() + _st.scrollPadding.bottom() + _padding.bottom();
	resize(newWidth, newHeight);
	_inner = rect().marginsRemoved(_padding);
}

not_null<QAction*> PopupMenu::addAction(
		base::unique_qptr<Menu::ItemBase> widget) {
	return _menu->addAction(std::move(widget));
}

not_null<QAction*> PopupMenu::addAction(const QString &text, Fn<void()> callback, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, std::move(callback), icon, iconOver);
}

not_null<QAction*> PopupMenu::addAction(const QString &text, std::unique_ptr<PopupMenu> submenu) {
	const auto action = _menu->addAction(text, std::make_unique<QMenu>());
	auto it = _submenus.insert(action, submenu.release());
	it.value()->setParent(parentWidget());
	it.value()->deleteOnHide(false);
	return action;
}

not_null<QAction*> PopupMenu::addSeparator() {
	return _menu->addSeparator();
}

void PopupMenu::clearActions() {
	for (const auto &submenu : base::take(_submenus)) {
		delete submenu;
	}
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
		showChildren();
	} else {
		paintBg(p);
	}
}

void PopupMenu::paintBg(QPainter &p) {
	if (_useTransparency) {
		Shadow::paint(p, _inner, width(), _st.shadow);
		_roundRect.paint(p, _inner);
	} else {
		p.fillRect(0, 0, width() - _padding.right(), _padding.top(), _st.shadow.fallback);
		p.fillRect(width() - _padding.right(), 0, _padding.right(), height() - _padding.bottom(), _st.shadow.fallback);
		p.fillRect(_padding.left(), height() - _padding.bottom(), width() - _padding.left(), _padding.bottom(), _st.shadow.fallback);
		p.fillRect(0, _padding.top(), _padding.left(), height() - _padding.top(), _st.shadow.fallback);
		p.fillRect(_inner, _st.menu.itemBg);
	}
}

void PopupMenu::handleActivated(const Menu::CallbackData &data) {
	if (data.source == TriggeredSource::Mouse) {
		if (!popupSubmenuFromAction(data)) {
			if (auto currentSubmenu = base::take(_activeSubmenu)) {
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
	if (auto submenu = _submenus.value(data.action)) {
		if (_activeSubmenu == submenu) {
			submenu->hideMenu(true);
		} else {
			popupSubmenu(submenu, data.actionTop, data.source);
		}
		return true;
	}
	return false;
}

void PopupMenu::popupSubmenu(SubmenuPointer submenu, int actionTop, TriggeredSource source) {
	if (auto currentSubmenu = base::take(_activeSubmenu)) {
		currentSubmenu->hideMenu(true);
	}
	if (submenu) {
		QPoint p(_inner.x() + (style::RightToLeft() ? _padding.right() : _inner.width() - _padding.left()), _inner.y() + actionTop);
		_activeSubmenu = submenu;
		_activeSubmenu->showMenu(geometry().topLeft() + p, this, source);

		_menu->setChildShown(true);
	} else {
		_menu->setChildShown(false);
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
	if (isHidden()) return;
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
		_activeSubmenu = SubmenuPointer();
	}
	if (!_hiding && !isHidden()) {
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
		if (_useTransparency) {
			_roundRect.paint(p, _inner);
		} else {
			p.fillRect(_inner, _st.menu.itemBg);
		}
		for (const auto child : children()) {
			if (const auto widget = qobject_cast<QWidget*>(child)) {
				RenderWidget(p, widget, widget->pos());
			}
		}
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
	activateWindow();
}

PopupMenu::~PopupMenu() {
	for (const auto &submenu : base::take(_submenus)) {
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
