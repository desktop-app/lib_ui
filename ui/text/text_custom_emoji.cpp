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

} // namespace Ui::Text
