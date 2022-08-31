// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_custom_emoji.h"

namespace Ui::Text {

int AdjustCustomEmojiSize(int emojiSize) {
	return base::SafeRound(emojiSize * 1.12);
}

ShiftedEmoji::ShiftedEmoji(
	std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
	QPoint shift)
: _wrapped(std::move(wrapped))
, _shift(shift) {
}

QString ShiftedEmoji::entityData() {
	return _wrapped->entityData();
}

void ShiftedEmoji::paint(QPainter &p, const Context &context) {
	auto copy = context;
	copy.position += _shift;
	_wrapped->paint(p, copy);
}

void ShiftedEmoji::unload() {
	_wrapped->unload();
}

bool ShiftedEmoji::ready() {
	return _wrapped->ready();
}

bool ShiftedEmoji::readyInDefaultState() {
	return _wrapped->readyInDefaultState();
}

FirstFrameEmoji::FirstFrameEmoji(std::unique_ptr<CustomEmoji> wrapped)
: _wrapped(std::move(wrapped)) {
}

QString FirstFrameEmoji::entityData() {
	return _wrapped->entityData();
}

void FirstFrameEmoji::paint(QPainter &p, const Context &context) {
	auto copy = context;
	copy.firstFrameOnly = true;
	_wrapped->paint(p, copy);
}

void FirstFrameEmoji::unload() {
	_wrapped->unload();
}

bool FirstFrameEmoji::ready() {
	return _wrapped->ready();
}

bool FirstFrameEmoji::readyInDefaultState() {
	return _wrapped->readyInDefaultState();
}

LimitedLoopsEmoji::LimitedLoopsEmoji(
	std::unique_ptr<CustomEmoji> wrapped,
	int limit)
: _wrapped(std::move(wrapped))
, _limit(limit) {
}

QString LimitedLoopsEmoji::entityData() {
	return _wrapped->entityData();
}

void LimitedLoopsEmoji::paint(QPainter &p, const Context &context) {
	if (_played < _limit) {
		if (_wrapped->readyInDefaultState()) {
			if (_inLoop) {
				_inLoop = false;
				++_played;
			}
		} else if (_wrapped->ready()) {
			_inLoop = true;
		}
	}
	if (_played == _limit) {
		const auto was = context.firstFrameOnly;
		context.firstFrameOnly = true;
		_wrapped->paint(p, context);
		context.firstFrameOnly = was;
	} else {
		_wrapped->paint(p, context);
	}
}

void LimitedLoopsEmoji::unload() {
	_wrapped->unload();
	_inLoop = false;
	_played = 0;
}

bool LimitedLoopsEmoji::ready() {
	return _wrapped->ready();
}

bool LimitedLoopsEmoji::readyInDefaultState() {
	return _wrapped->readyInDefaultState();
}

} // namespace Ui::Text
