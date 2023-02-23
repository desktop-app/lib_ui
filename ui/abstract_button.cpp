// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/abstract_button.h"

#include "ui/ui_utility.h"
#include "ui/integration.h"

#include <QtGui/QtEvents>

#include <rpl/filter.h>
#include <rpl/mappers.h>

namespace Ui {

AbstractButton::AbstractButton(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);

	using namespace rpl::mappers;
	shownValue()
		| rpl::filter(_1 == false)
		| rpl::start_with_next([this] { clearState(); }, lifetime());
}

void AbstractButton::leaveEventHook(QEvent *e) {
	if (_state & StateFlag::Down) {
		return;
	}

	setOver(false, StateChangeSource::ByHover);
	return TWidget::leaveEventHook(e);
}

void AbstractButton::enterEventHook(QEnterEvent *e) {
	checkIfOver(mapFromGlobal(QCursor::pos()));
	return TWidget::enterEventHook(e);
}

void AbstractButton::setAcceptBoth(bool acceptBoth) {
	_acceptBoth = acceptBoth;
}

void AbstractButton::checkIfOver(QPoint localPos) {
	auto over = rect().marginsRemoved(getMargins()).contains(localPos);
	setOver(over, StateChangeSource::ByHover);
}

void AbstractButton::mousePressEvent(QMouseEvent *e) {
	checkIfOver(e->pos());
	if (_state & StateFlag::Over) {
		const auto set = setDown(
			true,
			StateChangeSource::ByPress,
			e->modifiers(),
			e->button());
		if (set) {
			e->accept();
		}
	}
}

void AbstractButton::mouseMoveEvent(QMouseEvent *e) {
	if (rect().marginsRemoved(getMargins()).contains(e->pos())) {
		setOver(true, StateChangeSource::ByHover);
	} else {
		setOver(false, StateChangeSource::ByHover);
	}
}

void AbstractButton::mouseReleaseEvent(QMouseEvent *e) {
	const auto set = setDown(
		false,
		StateChangeSource::ByPress,
		e->modifiers(),
		e->button());
	if (set) {
		e->accept();
	}
}

void AbstractButton::clicked(
		Qt::KeyboardModifiers modifiers,
		Qt::MouseButton button) {
	_modifiers = modifiers;
	const auto weak = MakeWeak(this);
	if (button == Qt::LeftButton) {
		if (const auto callback = _clickedCallback) {
			callback();
		}
	}
	if (weak) {
		_clicks.fire_copy(button);
	}
}

void AbstractButton::setPointerCursor(bool enablePointerCursor) {
	if (_enablePointerCursor != enablePointerCursor) {
		_enablePointerCursor = enablePointerCursor;
		updateCursor();
	}
}

void AbstractButton::setOver(bool over, StateChangeSource source) {
	if (over == isOver()) {
		return;
	}
	const auto was = _state;
	if (over) {
		_state |= StateFlag::Over;
		Integration::Instance().registerLeaveSubscription(this);
	} else {
		_state &= ~State(StateFlag::Over);
		Integration::Instance().unregisterLeaveSubscription(this);
	}
	onStateChanged(was, source);
	updateCursor();
	update();
}

bool AbstractButton::setDown(
		bool down,
		StateChangeSource source,
		Qt::KeyboardModifiers modifiers,
		Qt::MouseButton button) {
	if (down
		&& !(_state & StateFlag::Down)
		&& (_acceptBoth || button == Qt::LeftButton)) {
		auto was = _state;
		_state |= StateFlag::Down;
		onStateChanged(was, source);
		return true;
	} else if (!down && (_state & StateFlag::Down)) {
		const auto was = _state;
		_state &= ~State(StateFlag::Down);

		const auto weak = MakeWeak(this);
		onStateChanged(was, source);
		if (weak) {
			if (was & StateFlag::Over) {
				clicked(modifiers, button);
			} else {
				setOver(false, source);
			}
		}
		return true;
	}
	return false;
}

void AbstractButton::updateCursor() {
	const auto pointerCursor = _enablePointerCursor && isOver();
	if (_pointerCursor != pointerCursor) {
		_pointerCursor = pointerCursor;
		setCursor(_pointerCursor ? style::cur_pointer : style::cur_default);
	}
}

void AbstractButton::setDisabled(bool disabled) {
	auto was = _state;
	if (disabled && !(_state & StateFlag::Disabled)) {
		_state |= StateFlag::Disabled;
		onStateChanged(was, StateChangeSource::ByUser);
	} else if (!disabled && (_state & StateFlag::Disabled)) {
		_state &= ~State(StateFlag::Disabled);
		onStateChanged(was, StateChangeSource::ByUser);
	}
}

void AbstractButton::clearState() {
	auto was = _state;
	_state = StateFlag::None;
	onStateChanged(was, StateChangeSource::ByUser);
}

} // namespace Ui
