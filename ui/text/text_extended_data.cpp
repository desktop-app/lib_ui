// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_extended_data.h"

#include "ui/text/text.h"

namespace Ui::Text {

SpoilerClickHandler::SpoilerClickHandler(
	not_null<String*> text,
	Fn<bool(const ClickContext&)> filter)
: _text(text)
, _filter(std::move(filter)) {
}

not_null<String*> SpoilerClickHandler::text() const {
	return _text;
}

void SpoilerClickHandler::setText(not_null<String*> text) {
	_text = text;
}

void SpoilerClickHandler::onClick(ClickContext context) const {
	if (_filter && !_filter(context)) {
		return;
	}
	_text->setSpoilerRevealed(true, anim::type::normal);
}

} // namespace Ui::Text

