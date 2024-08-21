// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_direction.h"

#include "ui/text/text_entity.h"

namespace style {
namespace {

bool RightToLeftValue = false;

} // namespace

bool RightToLeft() {
	return RightToLeftValue;
}

void SetRightToLeft(bool rtl) {
	RightToLeftValue = rtl;
}

} // namespace style

namespace st {

QString wrap_rtl(const QString &text) {
	const auto wrapper = QChar(rtl() ? 0x200F : 0x200E);

	auto result = QString();
	result.reserve(text.size() + 3);
	result
		.append(wrapper) // Don't override phrase direction by first symbol.
		.append(QChar(0x2068)) // Isolate tag content.
		.append(text)
		.append(QChar(0x2069)); // End of isolation.
	return result;
}

TextWithEntities wrap_rtl(const TextWithEntities &text) {
	const auto wrapper = QChar(rtl() ? 0x200F : 0x200E);

	auto result = TextWithEntities();
	result.reserve(text.text.size() + 3, text.entities.size());
	result
		.append(wrapper) // Don't override phrase direction by first symbol.
		.append(QChar(0x2068)) // Isolate tag content.
		.append(text)
		.append(QChar(0x2069)); // End of isolation.
	return result;
}

} // namespace st
