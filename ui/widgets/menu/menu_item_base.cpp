// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_item_base.h"

#include "ui/widgets/menu/menu.h"

#include <QtGui/QtEvents>

namespace Ui::Menu {

ItemBase::ItemBase(
	not_null<Menu*> parent,
	const style::Menu &st)
: RippleButton(parent, st.ripple)
, _menu(parent)
, _menuStyle(st) {
}

void ItemBase::paintBackground(
		QPainter &p,
		const QRect &rect,
		bool selected) {
	if (_menu->fluidHover()) {
		return;
	}
	const auto &st = _menuStyle;
	if (selected && st.itemBgOver->c.alpha() < 255) {
		p.fillRect(rect, st.itemBg);
	}
	p.fillRect(rect, selected ? st.itemBgOver : st.itemBg);
}

void ItemBase::setSelected(
		bool selected,
		TriggeredSource source) {
	if (selected && !isEnabled()) {
		return;
	}
	if (_selected.current() != selected) {
		setMouseTracking(!selected);
		_lastTriggeredSource = source;
		_selected = selected;
		update();
		if (selected && focusPolicy() != Qt::NoFocus) {
			setFocus();
			QAccessibleEvent event(this, QAccessible::Focus);
			QAccessible::updateAccessibility(&event);
		}
	}
}

bool ItemBase::isSelected() const {
	return _selected.current();
}

rpl::producer<CallbackData> ItemBase::selects() const {
	return _selected.changes(
	) | rpl::map([=](bool selected) -> CallbackData {
		return {
			action(),
			y(),
			_lastTriggeredSource,
			_index,
			selected,
			_preventClose,
		};
	});
}

TriggeredSource ItemBase::lastTriggeredSource() const {
	return _lastTriggeredSource;
}

int ItemBase::index() const {
	return _index;
}

void ItemBase::setIndex(int index) {
	_index = index;
}

void ItemBase::setClicked(TriggeredSource source) {
	if (isEnabled()) {
		_lastTriggeredSource = source;
		_clicks.fire({});
	}
}

rpl::producer<CallbackData> ItemBase::clicks() const {
	return rpl::merge(
		AbstractButton::clicks() | rpl::to_empty,
		_clicks.events()
	) | rpl::filter([=] {
		return isEnabled() && !AbstractButton::isDisabled();
	}) | rpl::map([=]() -> CallbackData {
		return {
			action(),
			y(),
			_lastTriggeredSource,
			_index,
			true,
			_preventClose,
		};
	});
}

rpl::producer<int> ItemBase::minWidthValue() const {
	return _minWidth.value();
}

int ItemBase::minWidth() const {
	return _minWidth.current();
}

void ItemBase::fitToMenuWidth() {
	_menu->widthValue() | rpl::on_next([=](int w) {
		if (w > 0) {
			resize(w, contentHeight());
		}
	}, lifetime());
}

void ItemBase::setMinWidth(int w) {
	_minWidth = w;
}

void ItemBase::setPreventClose(bool prevent) {
	_preventClose = prevent;
}

bool ItemBase::preventClose() const {
	return _preventClose;
}

void ItemBase::finishAnimating() {
	RippleButton::finishAnimating();
}

void ItemBase::enableMouseSelecting() {
	enableMouseSelecting(this);
}

void ItemBase::enableMouseSelecting(not_null<RpWidget*> widget) {
	widget->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		const auto fluid = _menu->fluidHover();
		if (((type == QEvent::Leave)
			|| (type == QEvent::Enter)
			|| (type == QEvent::MouseMove))
			&& (fluid || action()->isEnabled())) {
			const auto leave = (type == QEvent::Leave);
			const auto global = leave
				? QCursor::pos()
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
				: static_cast<QSinglePointEvent*>(
					e.get())->globalPosition().toPoint();
#else // Qt >= 6.0.0
				: (type == QEvent::Enter)
				? static_cast<QEnterEvent*>(e.get())->globalPos()
				: static_cast<QMouseEvent*>(e.get())->globalPos();
#endif // Qt < 6.0.0
			if (!leave) {
				_menu->setLastMouseGlobal(global);
			}
			if (_menu->mouseSelectionFrozen()) {
				return;
			} else if (fluid) {
				_menu->handleMouseMove(global);
			} else {
				setSelected(!leave);
			}
		} else if ((type == QEvent::MouseButtonRelease)
			&& isEnabled()
			&& isSelected()) {
			const auto point = mapFromGlobal(QCursor::pos());
			if (!rect().contains(point)) {
				setSelected(false);
			}
		}
	}, lifetime());
}

void ItemBase::setActionTriggered(Fn<void()> callback) {
	if (callback) {
		_connection = QObject::connect(
			action(),
			&QAction::triggered,
			std::move(callback));
	} else {
		_connection.reset();
	}
}

void ItemBase::keyPressEvent(QKeyEvent *e) {
	e->ignore();
}

void ItemBase::keyReleaseEvent(QKeyEvent *e) {
	e->ignore();
}

void ItemBase::mousePressEvent(QMouseEvent *e) {
	if (!_menu->hasMouseMoved(e->globalPos())) {
		return;
	}
	if (e->button() == Qt::LeftButton) {
		_mousePressed = true;
	}
	RippleButton::mousePressEvent(e);
}

void ItemBase::mouseMoveEvent(QMouseEvent *e) {
	_menu->mouseMoved();
	if (!_menu->hasMouseMoved(e->globalPos())) {
		return;
	}
	if (_mousePressed && _menu && !rect().contains(e->pos())) {
		_menu->handlePressedOutside(e->globalPos());
	}
	RippleButton::mouseMoveEvent(e);
}

void ItemBase::mouseReleaseEvent(QMouseEvent *e) {
	if (!_menu->hasMouseMoved(e->globalPos())) {
		return;
	}
	const auto wasPressed = base::take(_mousePressed);
#ifdef Q_OS_UNIX
	if (isEnabled() && e->button() == Qt::RightButton) {
		setClicked(TriggeredSource::Mouse);
		return;
	}
#endif // Q_OS_UNIX
	const auto isInRect = rect().contains(e->pos());
	if (isInRect
		&& isEnabled()
		&& e->button() == Qt::LeftButton
		&& !wasPressed) {
		//
		setClicked(TriggeredSource::Mouse);
		return;
	}
	if (wasPressed && _menu && !isInRect) {
		_menu->handleMouseRelease(e->globalPos());
	}
	RippleButton::mouseReleaseEvent(e);
}

} // namespace Ui::Menu
