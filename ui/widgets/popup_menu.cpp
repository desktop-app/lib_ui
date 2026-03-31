// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/popup_menu.h"

#include "base/platform/base_platform_info.h"
#include "base/invoke_queued.h"
#include "ui/image/image_prepare.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/delayed_activation.h"
#include "ui/painter.h"
#include "ui/integration.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"

#include <QtGui/QtEvents>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <private/qapplication_p.h>
#include <qpa/qplatformwindow_p.h>

namespace Ui {

PopupMenu::PopupMenu(QWidget *parent, const style::PopupMenu &st)
: RpWidget(parent)
, _st(st)
, _roundRect(_st.radius, _st.menu.itemBg)
, _boxShadow(_st.shadow)
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
, _roundRect(_st.radius, _st.menu.itemBg)
, _boxShadow(_st.shadow)
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
	) | rpl::on_next([=] {
		hideMenu(true);
	}, lifetime());

	_touchBeginCounter = Integration::Instance().touchCounterNow();

	installEventFilter(this);

	setupMenuWidget();

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::BypassWindowManagerHint
		| Qt::Popup
		| Qt::NoDropShadowWindowHint);
	setMouseTracking(true);

	hide();

	setAttribute(Qt::WA_NoSystemBackground, true);

	_useTransparency = Platform::TranslucentWindowsSupported();
	if (_useTransparency) {
		setAttribute(Qt::WA_TranslucentBackground, true);
	} else {
		setAttribute(Qt::WA_TranslucentBackground, false);
		setAttribute(Qt::WA_OpaquePaintEvent, true);
	}
}

not_null<PopupMenu*> PopupMenu::ensureSubmenu(
		not_null<QAction*> action,
		const style::PopupMenu &st) {
	const auto &list = actions();
	const auto found = ranges::find(list, action) != end(list);
	if (!found && _stashedContent) {
		const auto &stashedList = _stashedContent->menu->actions();
		Assert(ranges::find(stashedList, action) != end(stashedList));
	} else {
		Assert(found);
	}

	const auto j = _submenus.find(action);
	if (j != end(_submenus)) {
		return j->second.get();
	}
	const auto result = _submenus.emplace(
		action,
		base::make_unique_q<PopupMenu>(parentWidget(), st)
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

void PopupMenu::validateCompositingSupport() {
	const auto line = st::lineWidth;
	const auto &additional = _additionalMenuPadding;
	if (!_useTransparency) {
		_padding = QMargins(
			std::max(line, additional.left()),
			std::max(line, additional.top()),
			std::max(line, additional.right()),
			std::max(line, additional.bottom()));
		_margins = QMargins();
	} else {
		const auto ext = _boxShadow.extend();
		_padding = QMargins(
			std::max(ext.left(), additional.left()),
			std::max(ext.top(), additional.top()),
			std::max(ext.right(), additional.right()),
			std::max(ext.bottom(), additional.bottom()));
		_margins = _padding - (additional - _additionalMenuMargins);
	}
	Platform::SetWindowMargins(this, _margins);
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
	) | rpl::on_next([=](QSize size) {
		_roundingOverlay->setGeometry(QRect(QPoint(), size));
	}, _roundingOverlay->lifetime());

	_roundingOverlay->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		if (_inner.isEmpty()) {
			return;
		}
		auto p = QPainter(_roundingOverlay.data());
		auto hq = PainterHighQualityEnabler(p);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		_roundRect.paint(p, _inner, RectPart::AllCorners);
		if (!_grabbingForPanelAnimation) {
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			_boxShadow.paint(p, _inner, _st.radius);
		}
	}, _roundingOverlay->lifetime());

	_roundingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void PopupMenu::handleMenuResize() {
	if (_switchState) {
		return;
	}
	auto newWidth = _padding.left() + _st.scrollPadding.left() + _menu->width() + _st.scrollPadding.right() + _padding.right();
	auto newHeight = _padding.top() + _st.scrollPadding.top() + _menu->height() + _st.scrollPadding.bottom() + _padding.bottom();
	const auto wantedHeight = newHeight - _padding.top() - _padding.bottom();
	const auto scrollHeight = _st.maxHeight
		? std::min(_st.maxHeight, wantedHeight)
		: wantedHeight;
	_scroll->resize(
		newWidth - _padding.left() - _padding.right(),
		scrollHeight);
	{
		const auto newSize = QSize(
			newWidth,
			_padding.top() + scrollHeight + _padding.bottom());
		setFixedSize(newSize);
		resize(newSize);
	}
	_inner = rect().marginsRemoved(_padding);
}

not_null<QAction*> PopupMenu::addAction(
		base::unique_qptr<Menu::ItemBase> widget) {
	return _menu->addAction(std::move(widget));
}

not_null<QAction*> PopupMenu::addAction(
		const QString &text,
		Fn<void()> callback,
		const style::icon *icon,
		const style::icon *iconOver) {
	return _menu->addAction(text, std::move(callback), icon, iconOver);
}

not_null<QAction*> PopupMenu::addAction(
		const QString &text,
		std::unique_ptr<PopupMenu> submenu,
		const style::icon *icon,
		const style::icon *iconOver) {
	const auto action = _menu->addAction(
		text,
		std::make_unique<QMenu>(),
		icon,
		iconOver);
	const auto saved = _submenus.emplace(
		action,
		base::unique_qptr<PopupMenu>(submenu.release())
	).first->second.get();
	saved->setParent(parentWidget());
	saved->deleteOnHide(false);
	return action;
}

not_null<QAction*> PopupMenu::addSeparator(const style::MenuSeparator *st) {
	return _menu->addSeparator(st);
}

not_null<QAction*> PopupMenu::insertAction(
		int position,
		base::unique_qptr<Menu::ItemBase> widget) {
	return _menu->insertAction(position, std::move(widget));
}

void PopupMenu::removeAction(int position) {
	const auto i = _submenus.find(_menu->actions()[position]);
	if (i != end(_submenus)) {
		_submenus.erase(i);
	}
	_menu->removeAction(position);
}

void PopupMenu::clearActions() {
	_submenus.clear();
	return _menu->clearActions();
}

void PopupMenu::setTopShift(int topShift) {
	_topShift = topShift;
}

void PopupMenu::setForceWidth(int forceWidth) {
	_menu->setForceWidth(forceWidth);
	if (_stashedContent) {
		_stashedContent->menu->setForceWidth(forceWidth);
	}
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
		const auto opacity = _a_opacity.value(_hiding ? 0. : 1.);
		const auto progress = _a_show.value(1.);
		const auto state = (opacity > 0.)
			? _showAnimation->paintFrame(p, 0, 0, width(), progress, opacity)
			: PanelAnimation::PaintState();
		_showStateChanges.fire({
			.opacity = state.opacity,
			.widthProgress = state.widthProgress,
			.heightProgress = state.heightProgress,
			.appearingWidth = state.width,
			.appearingHeight = state.height,
			.appearing = true,
		});
	} else if (_a_opacity.animating()) {
		if (_showAnimation) {
			_showAnimation.reset();
			_showStateChanges.fire({
				.toggling = true,
			});
		}
		p.setOpacity(_a_opacity.value(0.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else if (_showAnimation) {
		_showAnimation->paintFrame(p, 0, 0, width(), 1., 1.);
		_showAnimation.reset();
		_showStateChanges.fire({});
		PostponeCall(this, [=] {
			showChildren();
			_animatePhase = AnimatePhase::Shown;
			Platform::AcceptAllMouseInput(this);
		});
	} else {
		paintBg(p);
	}
}

void PopupMenu::paintBg(QPainter &p) {
	if (!_useTransparency) {
		p.fillRect(0, 0, width() - _padding.right(), _padding.top(), _st.shadowFallback);
		p.fillRect(width() - _padding.right(), 0, _padding.right(), height() - _padding.bottom(), _st.shadowFallback);
		p.fillRect(_padding.left(), height() - _padding.bottom(), width() - _padding.left(), _padding.bottom(), _st.shadowFallback);
		p.fillRect(0, _padding.top(), _padding.left(), height() - _padding.top(), _st.shadowFallback);
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
		if (!data.preventClose) {
			hideMenu();
		}
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
		const auto padding = _useTransparency
			? _boxShadow.extend()
			: QMargins(st::lineWidth, 0, st::lineWidth, 0);
		QPoint p(_inner.x() + (style::RightToLeft() ? padding.right() : (_inner.width() - padding.left())), _inner.y() + actionTop);
		_activeSubmenu = submenu;
		_activeSubmenu->menu()->clearSelection();
		_activeSubmenu->setAccessibleName(action->text());
		if (_activeSubmenu->prepareGeometryFor(geometry().topLeft() + p, this)) {
			_activeSubmenu->showPrepared(source);
			_menu->setChildShownAction(action);
		} else {
			_activeSubmenu = nullptr;
		}
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
	if (!InFocusChain(this)) {
		hideMenu();
	}
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
	// Mouse presses, synthesized from touch events,
	// should be ignored, if the touch, that caused
	// them, started before the menu was created.
	if (e->source() != Qt::MouseEventSynthesizedBySystem
		|| (Integration::Instance().touchCounterNow()
			> _touchBeginCounter)) {
		forwardMousePress(e->globalPos());
	}
}

bool PopupMenu::eventFilter(QObject *o, QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd) {
		if (o == windowHandle() && isActiveWindow()) {
			const auto event = static_cast<QTouchEvent*>(e);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
			e->setAccepted(
				QApplicationPrivate::translateRawTouchEvent(
					this,
					event->device(),
					event->touchPoints(),
					event->timestamp()));
#elif QT_VERSION < QT_VERSION_CHECK(6, 2, 0) // Qt < 6.0.0
			e->setAccepted(
				QApplicationPrivate::translateRawTouchEvent(
					this,
					event->pointingDevice(),
					const_cast<QList<QEventPoint> &>(event->points()),
					event->timestamp()));
#else // Qt < 6.2.0
			e->setAccepted(
				QApplicationPrivate::translateRawTouchEvent(this, event));
#endif
			return e->isAccepted();
		}
	}
	return false;
}

void PopupMenu::hideMenu(bool fast) {
	if (isHidden() || (_hiding && !fast)) {
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

void PopupMenu::setForcedVerticalOrigin(VerticalOrigin origin) {
	_forcedVerticalOrigin = origin;
}

void PopupMenu::setAdditionalMenuPadding(
		QMargins padding,
		QMargins margins) {
	Expects(padding.left() >= margins.left()
		&& padding.right() >= margins.right()
		&& padding.top() >= margins.top()
		&& padding.bottom() >= margins.bottom());

	if (_additionalMenuPadding != padding
		|| _additionalMenuMargins != margins) {
		_additionalMenuPadding = padding;
		_additionalMenuMargins = margins;
		_roundingOverlay.destroy();
	}
}

void PopupMenu::showAnimated(PanelAnimation::Origin origin) {
	setOrigin(origin);
	showStarted();
}

void PopupMenu::hideAnimated() {
	if (isHidden() || _hiding) {
		return;
	}
	if (!_keepingDelayedActivationPaused) {
		_keepingDelayedActivationPaused = true;
		KeepDelayedActivationPaused(true);
	}
	startOpacityAnimation(true);
}

void PopupMenu::hideFast() {
	if (isHidden()) return;

	_a_opacity.stop();
	hideFinished();
}

void PopupMenu::hideFinished() {
	_hiding = false;
	_a_show.stop();
	_cache = QPixmap();
	_animatePhase = AnimatePhase::Hidden;
	if (!isHidden()) {
		hide();
	}
}

void PopupMenu::prepareCache() {
	if (_a_opacity.animating()) return;

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	if (showAnimation.animating()) {
		_showStateChanges.fire({});
	}
	showChildren();
	_cache = GrabWidget(this);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
	if (_a_show.animating()) {
		fireCurrentShowState();
	}
}

void PopupMenu::startOpacityAnimation(bool hiding) {
	if (!_useTransparency) {
		_a_opacity.stop();
		_hiding = hiding;
		if (_hiding) {
			InvokeQueued(this, [=] {
				if (_hiding) {
					hideFinished();
				}
			});
		} else {
			update();
		}
		return;
	}
	_hiding = false;
	prepareCache();
	_hiding = hiding;
	_animatePhase = hiding
		? AnimatePhase::StartHide
		: AnimatePhase::StartShow;
	hideChildren();
	_a_opacity.start(
		[=] { opacityAnimationCallback(); },
		_hiding ? 1. : 0.,
		_hiding ? 0. : 1.,
		_st.duration);
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
		_showAnimation->setFinalImage(std::move(cache), QRect(_inner.topLeft() * pixelRatio, _inner.size() * pixelRatio), _st.radius);
		if (_useTransparency) {
			_showAnimation->setCornerMasks(Images::CornersMask(_st.radius));
		} else {
			_showAnimation->setSkipShadow(true);
		}
		_showAnimation->start();
	}
	_animatePhase = AnimatePhase::StartShow;
	hideChildren();
	_a_show.start([this] { showAnimationCallback(); }, 0., 1., _st.showDuration);
	fireCurrentShowState();
}

void PopupMenu::fireCurrentShowState() {
	const auto state = _showAnimation->computeState(
		_a_show.value(1.),
		_a_opacity.value(1.));
	_showStateChanges.fire({
		.opacity = state.opacity,
		.widthProgress = state.widthProgress,
		.heightProgress = state.heightProgress,
		.appearingWidth = state.width,
		.appearingHeight = state.height,
		.appearing = true,
	});
}

void PopupMenu::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			hideFinished();
		} else {
			showChildren();
			_animatePhase = AnimatePhase::Shown;
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

QPoint PopupMenu::ConstrainToParentScreen(
		not_null<PopupMenu*> menu,
		QPoint globalPos) {
	auto result = globalPos;
	if (const auto parent = menu->parentWidget()) {
		if (const auto parentScreen = parent->window()->screen()) {
			const auto r = parentScreen->availableGeometry();
			if (!r.contains(result)) {
				result.setX(std::clamp(result.x(), r.left(), r.right()));
				result.setY(std::clamp(result.y(), r.top(), r.bottom()));
			}
		}
	}
	return result;
}

void PopupMenu::popup(const QPoint &p) {
	if (prepareGeometryFor(p)) {
		popupPrepared();
		return;
	}
	_hiding = false;
	_a_opacity.stop();
	_a_show.stop();
	_cache = QPixmap();
	hide();
	if (_deleteOnHide) {
		deleteLater();
	}
}

void PopupMenu::popupPrepared() {
	showPrepared(TriggeredSource::Mouse);
}

PanelAnimation::Origin PopupMenu::preparedOrigin() const {
	return _origin;
}

QMargins PopupMenu::preparedPadding() const {
	return _padding;
}



QMargins PopupMenu::additionalMenuPadding() const {
	return _additionalMenuPadding;
}

QMargins PopupMenu::additionalMenuMargins() const {
	return _additionalMenuMargins;
}

QMargins PopupMenu::preparedMargins() const {
	return _margins;
}

bool PopupMenu::useTransparency() const {
	return _useTransparency;
}

int PopupMenu::scrollTop() const {
	return _scroll->scrollTop();
}

rpl::producer<int> PopupMenu::scrollTopValue() const {
	return _scroll->scrollTopValue();
}

rpl::producer<PopupMenu::ShowState> PopupMenu::showStateValue() const {
	return _showStateChanges.events();
}

bool PopupMenu::prepareGeometryFor(const QPoint &p) {
	return prepareGeometryFor(p, nullptr);
}

bool PopupMenu::prepareGeometryFor(const QPoint &p, PopupMenu *parent) {
	if (_clearLastSeparator) {
		_menu->clearLastSeparator();
		for (const auto &[action, submenu] : _submenus) {
			submenu->menu()->clearLastSeparator();
		}
	}

	_parent = parent;
	const auto screen = QGuiApplication::screenAt(p);

	createWinId();
	windowHandle()->removeEventFilter(this);
	windowHandle()->installEventFilter(this);
	if (_parent) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		setScreen(_parent->screen());
#else // Qt >= 6.0.0
		windowHandle()->setScreen(_parent->screen());
#endif // Qt < 6.0.0
	} else if (screen) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		setScreen(screen);
#else // Qt >= 6.0.0
		windowHandle()->setScreen(screen);
#endif // Qt < 6.0.0
	}
	validateCompositingSupport();

	using Origin = PanelAnimation::Origin;
	auto origin = Origin::TopLeft;
	const auto forceLeft = _forcedOrigin
		&& (*_forcedOrigin == Origin::TopLeft
			|| *_forcedOrigin == Origin::BottomLeft);
	const auto forceTop = (_forcedVerticalOrigin
		&& (*_forcedVerticalOrigin == VerticalOrigin::Top))
		|| (_forcedOrigin
			&& (*_forcedOrigin == Origin::TopLeft
				|| *_forcedOrigin == Origin::TopRight));
	const auto forceRight = _forcedOrigin
		&& (*_forcedOrigin == Origin::TopRight
			|| *_forcedOrigin == Origin::BottomRight);
	const auto forceBottom = (_forcedVerticalOrigin
		&& (*_forcedVerticalOrigin == VerticalOrigin::Bottom))
		|| (_forcedOrigin
			&& (*_forcedOrigin == Origin::BottomLeft
				|| *_forcedOrigin == Origin::BottomRight));
	auto w = p - QPoint(
		std::max(
			_additionalMenuPadding.left() - _boxShadow.extend().left(),
			0),
		_padding.top() - _topShift);
	auto r = screen ? screen->availableGeometry() : QRect();
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0) && defined QT_FEATURE_wayland && QT_CONFIG(wayland)
	using namespace QNativeInterface::Private;
	if (const auto native
			= windowHandle()->nativeInterface<QWaylandWindow>()) {
		const auto padding = _additionalMenuPadding - _additionalMenuMargins;
		base::take(r);
		if (_parent) {
			// we must have an action to position the submenu around
			const auto action = not_null(
				_parent->menu()->findSelectedAction());
			native->setParentControlGeometry(
				QRect(
					action->mapTo(action->window(), QPoint()),
					action->size()) + _st.scrollPadding);
		} else if (padding.top()) {
			// provide the compositor with a range for flip_y so it uses
			// the cursor point instead of the padding's top point
			native->setParentControlGeometry(
				QRect(
					p
						- parentWidget()->window()->pos()
						- QPoint(padding.left(), padding.top()),
					QSize(1, padding.top())));
			windowHandle()->setProperty(
				"_q_waylandPopupAnchor",
				QVariant::fromValue(Qt::TopEdge | Qt::LeftEdge));
		}
		native->setExtendedWindowType(_parent
			? QWaylandWindow::SubMenu
			: QWaylandWindow::Menu);
	}
#endif // Qt >= 6.11.0 && wayland
	const auto parentWidth = _parent ? _parent->inner().width() : 0;
	if (style::RightToLeft()) {
		const auto badLeft = !r.isNull() && w.x() - width() < r.x() - _margins.left();
		if (forceRight || (badLeft && !forceLeft)) {
			if (_parent && (r.isNull() || w.x() + parentWidth - _margins.left() - _margins.right() + width() - _margins.right() <= r.x() + r.width())) {
				w.setX(w.x() + parentWidth - _margins.left() - _margins.right());
			} else {
				w.setX(r.x() - _margins.left());
			}
		} else {
			w.setX(w.x() - width());
		}
	} else {
		const auto badLeft = !r.isNull() && w.x() + width() - _margins.right() > r.x() + r.width();
		if (forceRight || (badLeft && !forceLeft)) {
			if (_parent && (r.isNull() || w.x() - parentWidth + _margins.left() + _margins.right() - width() + _margins.right() >= r.x() - _margins.left())) {
				w.setX(w.x() + _margins.left() + _margins.right() - parentWidth - width() + _margins.left() + _margins.right());
			} else {
				w.setX(p.x() - width() + std::max(
					_additionalMenuPadding.right() - _boxShadow.extend().right(),
					0));
			}
			origin = PanelAnimation::Origin::TopRight;
		}
	}
	const auto badTop = !r.isNull() && w.y() + height() - _margins.bottom() > r.y() + r.height();
	if (forceBottom || (badTop && !forceTop)) {
		if (_parent) {
			w.setY(r.y() + r.height() - height() + _margins.bottom());
		} else {
			w.setY(p.y() - height() + _margins.bottom());
			origin = (origin == PanelAnimation::Origin::TopRight)
				? PanelAnimation::Origin::BottomRight
				: PanelAnimation::Origin::BottomLeft;
		}
	}
	if (!r.isNull()) {
		if (w.x() + width() - _margins.right() > r.x() + r.width()) {
			w.setX(r.x() + r.width() + _margins.right() - width());
		}
		if (w.x() + _margins.left() < r.x()) {
			w.setX(r.x() - _margins.left());
		}
		if (w.y() + height() - _margins.bottom() > r.y() + r.height()) {
			w.setY(r.y() + r.height() + _margins.bottom() - height());
		}
		if (w.y() + _margins.top() < r.y()) {
			w.setY(r.y() - _margins.top());
		}
	}
	move(w);

	setOrigin(origin);
	return true;
}

void PopupMenu::showPrepared(TriggeredSource source) {
	startShowAnimation();

	if (::Platform::IsWindows()) {
		ForceFullRepaintSync(this);
	}
	show();
	Platform::ShowOverAll(this);
	raise();
	activateWindow();
	if (Ui::ScreenReaderModeActive()) {
		_menu->setShowSource(TriggeredSource::Keyboard);
	} else {
		_menu->setShowSource(source);
	}
}

void PopupMenu::setClearLastSeparator(bool clear) {
	_clearLastSeparator = clear;
}


void PopupMenu::finishSwitchAnimation() {
	if (!_switchState) {
		return;
	}
	_switchState->overlay.destroy();
	_switchState.reset();
	_scroll->show();
	handleMenuResize();
}

void PopupMenu::setupMenuWidget() {
	const auto paddingWrap = static_cast<PaddingWrap<Menu::Menu>*>(
		_menu->parentWidget());

	paddingWrap->paintRequest(
	) | rpl::on_next([=](QRect clip) {
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

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_menu->heightValue()
	) | rpl::on_next([=](int scrollTop, int scrollHeight, int) {
		const auto scrollBottom = scrollTop + scrollHeight;
		paddingWrap->setVisibleTopBottom(scrollTop, scrollBottom);
	}, paddingWrap->lifetime());

	_menu->scrollToRequests(
	) | rpl::on_next([=](ScrollToRequest request) {
		_scroll->scrollTo({
			request.ymin ? (_st.scrollPadding.top() + request.ymin) : 0,
			(request.ymax == _menu->height()
				? paddingWrap->height()
				: (_st.scrollPadding.top() + request.ymax)),
		});
	}, _menu->lifetime());

	_menu->resizesFromInner(
	) | rpl::on_next([=] {
		handleMenuResize();
	}, _menu->lifetime());
	_menu->setActivatedCallback([this](const Menu::CallbackData &data) {
		handleActivated(data);
	});
	_menu->setTriggeredCallback([this](const Menu::CallbackData &data) {
		handleTriggered(data);
	});
	_menu->setKeyPressDelegate([this](int key) {
		return handleKeyPress(key);
	});
	_menu->setMouseMoveDelegate([this](QPoint globalPosition) {
		handleMouseMove(globalPosition);
	});
	_menu->setMousePressDelegate([this](QPoint globalPosition) {
		handleMousePress(globalPosition);
	});
	_menu->setMouseReleaseDelegate([this](QPoint globalPosition) {
		handleMouseRelease(globalPosition);
	});
}

void PopupMenu::swapWithStashed() {
	Assert(_stashedContent != nullptr);

	// Take current content out of scroll.
	auto currentWrap = _scroll->takeWidget<QWidget>();
	auto currentMenu = _menu;

	// Restore stashed content into scroll.
	// Subscriptions and delegates are still alive on the stashed widgets.
	_scroll->setOwnedWidget(std::move(_stashedContent->wrap));
	_menu = _stashedContent->menu;

	// Stash current content.
	_stashedContent->wrap = std::move(currentWrap);
	_stashedContent->menu = currentMenu;

	// _submenus stays shared — actions from both pages use it.
}

void PopupMenu::stashContent(Fn<void(not_null<PopupMenu*>)> fillNew) {
	if (_switchState) {
		_switchState->animation.stop();
		_switchState->overlay.destroy();
		_switchState.reset();
		_scroll->show();
	}

	// Stash current content. _submenus stays shared.
	_stashedContent = std::make_unique<StashedContent>(StashedContent{
		.wrap = _scroll->takeWidget<QWidget>(),
		.menu = _menu,
	});

	// Create new menu in scroll.
	auto wrap = object_ptr<PaddingWrap<Menu::Menu>>(
		_scroll.data(),
		object_ptr<Menu::Menu>(_scroll.data(), _st.menu),
		_st.scrollPadding);
	_menu = wrap->entity();
	_scroll->setOwnedWidget(std::move(wrap));
	setupMenuWidget();

	// Fill new page.
	fillNew(this);

	// Equalize widths between stashed and new page.
	const auto maxWidth = std::max(
		_menu->width(),
		_stashedContent->menu->width());
	_menu->setForceWidth(maxWidth);
	_stashedContent->menu->setForceWidth(maxWidth);

	handleMenuResize();
}

void PopupMenu::swapStashed(SwitchDirection direction) {
	if (!_stashedContent) {
		return;
	}
	if (_switchState && _switchState->animation.animating()) {
		const auto raw = _switchState.get();
		const auto progress = raw->animation.value(1.);
		raw->animation.stop();

		swapWithStashed();

		std::swap(raw->oldSnapshot, raw->newSnapshot);
		std::swap(raw->fromScrollHeight, raw->toScrollHeight);
		raw->direction = direction;
		startSwitchAnimation(raw, 1. - progress);
		return;
	}
	if (_switchState) {
		_switchState->overlay.destroy();
		_switchState.reset();
		_scroll->show();
		handleMenuResize();
	}

	SendPendingMoveResizeEvents(this);
	const auto oldPixmap = GrabWidget(_menu);
	const auto scrollWidth = _scroll->width();
	const auto oldScrollHeight = _scroll->height();

	// Swap widgets.
	swapWithStashed();
	SendPendingMoveResizeEvents(_menu);

	const auto newPixmap = GrabWidget(_menu);
	const auto newMenuHeight = _menu->height();
	const auto wantedHeight = _st.scrollPadding.top()
		+ newMenuHeight
		+ _st.scrollPadding.bottom();
	const auto newScrollHeight = _st.maxHeight
		? std::min(_st.maxHeight, wantedHeight)
		: wantedHeight;

	_switchState = std::make_unique<SwitchState>();
	_switchState->direction = direction;
	_switchState->fromScrollHeight = oldScrollHeight;
	_switchState->oldSnapshot = oldPixmap;
	_switchState->newSnapshot = newPixmap;
	_switchState->toScrollHeight = newScrollHeight;

	_scroll->hide();

	// Resize popup to max(old, new) height once upfront instead of
	// calling setFixedSize/resize on every frame. Per-frame resizing of
	// translucent top-level windows can cause compositor jitter on macOS.
	// The visual height transition is achieved by animating _inner and
	// clipping the overlay; the extra transparent area is invisible.
	const auto maxScrollHeight = std::max(oldScrollHeight, newScrollHeight);
	{
		const auto maxSize = QSize(
			_padding.left() + scrollWidth + _padding.right(),
			_padding.top() + maxScrollHeight + _padding.bottom());
		setFixedSize(maxSize);
		resize(maxSize);
	}
	_inner = QRect(
		_padding.left(),
		_padding.top(),
		scrollWidth,
		oldScrollHeight);

	const auto raw = _switchState.get();
	raw->overlay.create(this);
	raw->overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->overlay->move(_padding.left(), _padding.top());
	raw->overlay->resize(scrollWidth, oldScrollHeight);
	raw->overlay->show();
	if (_roundingOverlay) {
		_roundingOverlay->raise();
	}

	const auto scrollPadding = _st.scrollPadding;

	raw->overlay->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		if (!_switchState || _switchState.get() != raw) {
			return;
		}
		if (!raw->overlay->width() || !raw->overlay->height()) {
			return;
		}
		auto p = QPainter(raw->overlay.data());
		p.fillRect(raw->overlay->rect(), _st.menu.itemBg);

		const auto progress = raw->animation.value(1.);
		const auto dir = (raw->direction == SwitchDirection::LeftToRight)
			? 1
			: -1;
		const auto shift = anim::interpolate(0, scrollWidth, progress);

		p.drawPixmap(
			scrollPadding.left() - dir * shift,
			scrollPadding.top(),
			raw->oldSnapshot);
		p.drawPixmap(
			scrollPadding.left() + dir * (scrollWidth - shift),
			scrollPadding.top(),
			raw->newSnapshot);
	}, raw->overlay->lifetime());

	startSwitchAnimation(raw, 0.);
}

void PopupMenu::startSwitchAnimation(
		not_null<SwitchState*> raw,
		float64 from) {
	const auto scrollWidth = raw->overlay->width();
	raw->animation.start([=] {
		if (!_switchState || _switchState.get() != raw) {
			return;
		}
		const auto progress = raw->animation.value(1.);
		const auto h = anim::interpolate(
			raw->fromScrollHeight,
			raw->toScrollHeight,
			progress);

		raw->overlay->resize(scrollWidth, h);
		raw->overlay->update();

		_inner = QRect(
			_padding.left(),
			_padding.top(),
			scrollWidth,
			h);
		update();

		if (!raw->animation.animating()) {
			PostponeCall(this, [=] {
				if (_switchState.get() == raw) {
					finishSwitchAnimation();
				}
			});
		}
	}, from, 1., _st.showDuration * (1. - from), anim::sineInOut);
}

bool PopupMenu::hasStashedContent() const {
	return _stashedContent != nullptr;
}

RpWidget *PopupMenu::accessibilityParent() const {
	return qobject_cast<RpWidget*>(parentWidget());
}

PopupMenu::~PopupMenu() {
	_stashedContent.reset();
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
	if (_keepingDelayedActivationPaused) {
		KeepDelayedActivationPaused(false);
	}
	if (_destroyedCallback) {
		_destroyedCallback();
	}
}

} // namespace Ui
