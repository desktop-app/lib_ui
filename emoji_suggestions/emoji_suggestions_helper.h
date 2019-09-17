// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "emoji_suggestions.h"
#include "emoji_suggestions_data.h"

namespace Ui {
namespace Emoji {

inline utf16string QStringToUTF16(const QString &string) {
	return utf16string(
		reinterpret_cast<const utf16char*>(string.constData()),
		string.size());
}

inline QString QStringFromUTF16(utf16string string) {
	return QString::fromRawData(
		reinterpret_cast<const QChar*>(string.data()),
		string.size());
}

constexpr auto kSuggestionMaxLength = internal::kReplacementMaxLength;

} // namespace Emoji
} // namespace Ui
