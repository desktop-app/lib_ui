// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_custom_emoji.h"

#include "ui/text/text_utilities.h"
#include "ui/text/text.h"

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

int ShiftedEmoji::width() {
	return _wrapped->width();
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

int FirstFrameEmoji::width() {
	return _wrapped->width();
}

QString FirstFrameEmoji::entityData() {
	return _wrapped->entityData();
}

void FirstFrameEmoji::paint(QPainter &p, const Context &context) {
	const auto was = context.internal.forceFirstFrame;
	context.internal.forceFirstFrame = true;
	_wrapped->paint(p, context);
	context.internal.forceFirstFrame = was;
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
	int limit,
	bool stopOnLast)
: _wrapped(std::move(wrapped))
, _limit(limit)
, _stopOnLast(stopOnLast) {
}

int LimitedLoopsEmoji::width() {
	return _wrapped->width();
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
		const auto wasFirst = context.internal.forceFirstFrame;
		const auto wasLast = context.internal.forceLastFrame;
		(_stopOnLast
			? context.internal.forceLastFrame
			: context.internal.forceFirstFrame) = true;
		_wrapped->paint(p, context);
		context.internal.forceFirstFrame = wasFirst;
		context.internal.forceLastFrame = wasLast;
	} else if (_played + 1 == _limit && _inLoop && _stopOnLast) {
		const auto wasLast = context.internal.overrideFirstWithLastFrame;
		context.internal.overrideFirstWithLastFrame = true;
		_wrapped->paint(p, context);
		context.internal.overrideFirstWithLastFrame = wasLast;
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

std::unique_ptr<CustomEmoji> MakeCustomEmoji(
		QStringView data,
		const MarkedContext &context) {
	if (auto simple = TryMakeSimpleEmoji(data)) {
		return simple;
	} else if (const auto &factory = context.customEmojiFactory) {
		return factory(data, context);
	}
	return nullptr;
}

StaticCustomEmoji::StaticCustomEmoji(QImage &&image, QString entity)
: _image(std::move(image))
, _entity(std::move(entity)) {
}

int StaticCustomEmoji::width() {
	return _image.width() / style::DevicePixelRatio();
}

QString StaticCustomEmoji::entityData() {
	return _entity;
}

void StaticCustomEmoji::paint(QPainter &p, const Context &context) {
	p.drawImage(context.position, _image);
}

void StaticCustomEmoji::unload() {
	_image = QImage();
}

bool StaticCustomEmoji::ready() {
	return true;
}

bool StaticCustomEmoji::readyInDefaultState() {
	return true;
}

} // namespace Ui::Text
