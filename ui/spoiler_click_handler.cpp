// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/spoiler_click_handler.h"

#include "ui/effects/animation_value.h"
#include "ui/text/text_entity.h"

ClickHandler::TextEntity SpoilerClickHandler::getTextEntity() const {
	return { EntityType::Spoiler };
}

void SpoilerClickHandler::onClick(ClickContext context) const {
	if (!_shown) {
		const auto nonconst = const_cast<SpoilerClickHandler*>(this);
		nonconst->_shown = true;
	}
}

bool SpoilerClickHandler::shown() const {
	return _shown;
}

crl::time SpoilerClickHandler::startMs() const {
	return _startMs;
}

void SpoilerClickHandler::setStartMs(crl::time value) {
	if (anim::Disabled()) {
		return;
	}
	_startMs = value;
}

void SpoilerClickHandler::setShown(bool value) {
	_shown = value;
}
