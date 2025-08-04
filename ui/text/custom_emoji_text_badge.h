// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core_types.h"
#include "ui/text/custom_emoji_helper.h"

namespace style {
struct RoundButton;
} // namespace style

namespace st {
extern const style::RoundButton &customEmojiTextBadge;
extern const style::margins &customEmojiTextBadgeMargin;
} // namespace st

namespace Ui::Text {

[[nodiscard]] PaletteDependentEmoji CustomEmojiTextBadge(
	const QString &text,
	const style::RoundButton &st = st::customEmojiTextBadge,
	const style::margins &margin = st::customEmojiTextBadgeMargin);

} // namespace Ui::Text
