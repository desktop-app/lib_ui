// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/call_mute_button.h"

#include "styles/style_widgets.h"

namespace Ui {

CallMuteButton::CallMuteButton(
	not_null<QWidget*> parent,
	CallMuteButtonState initial)
: _state(initial)
, _content(parent, st::callMuteButtonActive, &st::callMuteButtonMuted)
, _connecting(parent, st::callMuteButtonConnecting) {
	if (_state.type == CallMuteButtonType::Connecting
		|| _state.type == CallMuteButtonType::ForceMuted) {
		_connecting.setText(rpl::single(_state.text));
		_connecting.show();
		_content.hide();
	} else {
		_content.setText(rpl::single(_state.text));
		_content.setProgress((_state.type == CallMuteButtonType::Muted) ? 1. : 0.);
		_connecting.hide();
		_content.show();
	}
	_connecting.setAttribute(Qt::WA_TransparentForMouseEvents);
}

void CallMuteButton::setState(const CallMuteButtonState &state) {
	if (state.type == CallMuteButtonType::Connecting
		|| state.type == CallMuteButtonType::ForceMuted) {
		if (_state.text != state.text) {
			_connecting.setText(rpl::single(state.text));
		}
		if (!_connecting.isHidden() || !_content.isHidden()) {
			_connecting.show();
		}
		_content.setOuterValue(0.);
		_content.hide();
	} else {
		if (_state.text != state.text) {
			_content.setText(rpl::single(state.text));
		}
		_content.setProgress((state.type == CallMuteButtonType::Muted) ? 1. : 0.);
		if (!_connecting.isHidden() || !_content.isHidden()) {
			_content.show();
		}
		_connecting.hide();
		if (state.type == CallMuteButtonType::Active) {
			_content.setOuterValue(_level);
		} else {
			_content.setOuterValue(0.);
		}
	}
	_state = state;
}

void CallMuteButton::setLevel(float level) {
	_level = level;
	if (_state.type == CallMuteButtonType::Active) {
		_content.setOuterValue(level);
	}
}

rpl::producer<Qt::MouseButton> CallMuteButton::clicks() const {
	return _content.clicks();
}

QSize CallMuteButton::innerSize() const {
	return innerGeometry().size();
}

QRect CallMuteButton::innerGeometry() const {
	const auto skip = st::callMuteButtonActive.outerRadius;
	return QRect(
		_content.x(),
		_content.y(),
		_content.width() - 2 * skip,
		_content.width() - 2 * skip);
}

void CallMuteButton::moveInner(QPoint position) {
	const auto skip = st::callMuteButtonActive.outerRadius;
	_content.move(position - QPoint(skip, skip));
	_connecting.move(_content.pos());
}

void CallMuteButton::setVisible(bool visible) {
	if (!visible) {
		_content.hide();
		_connecting.hide();
	} else if (_state.type == CallMuteButtonType::Connecting
		|| _state.type == CallMuteButtonType::ForceMuted) {
		_connecting.show();
	} else {
		_content.show();
	}
}

void CallMuteButton::raise() {
	_content.raise();
	_connecting.raise();
}

void CallMuteButton::lower() {
	_content.lower();
	_connecting.lower();
}

rpl::lifetime &CallMuteButton::lifetime() {
	return _content.lifetime();
}

} // namespace Ui
