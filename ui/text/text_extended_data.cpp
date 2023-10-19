// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_extended_data.h"

#include "ui/text/text.h"
#include "ui/integration.h"

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

PreClickHandler::PreClickHandler(
	not_null<String*> text,
	uint16 offset,
	uint16 length)
: _text(text)
, _offset(offset)
, _length(length) {
}

not_null<String*> PreClickHandler::text() const {
	return _text;
}

void PreClickHandler::setText(not_null<String*> text) {
	_text = text;
}

void PreClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	const auto till = uint16(_offset + _length);
	auto text = _text->toTextForMimeData({ _offset, till });
	if (text.empty()) {
		return;
	} else if (!text.rich.text.endsWith('\n')) {
		text.rich.text.append('\n');
	}
	if (!text.expanded.endsWith('\n')) {
		text.expanded.append('\n');
	}
	if (Integration::Instance().copyPreOnClick(context.other)) {
		TextUtilities::SetClipboardText(std::move(text));
	}
}

} // namespace Ui::Text

