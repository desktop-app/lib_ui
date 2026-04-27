// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text_entity.h"

#include <optional>

namespace TextUtilities {

[[nodiscard]] QString TextWithTagsToHtml(const TextWithTags &text);
[[nodiscard]] std::optional<TextWithTags> TextWithTagsFromHtml(
	QStringView html);

} // namespace TextUtilities
