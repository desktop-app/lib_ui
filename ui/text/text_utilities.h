// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text_entity.h"

namespace Ui {
namespace Text {
namespace details {

struct ToUpperType {
	inline QString operator()(const QString &text) const {
		return text.toUpper();
	}
	inline QString operator()(QString &&text) const {
		return std::move(text).toUpper();
	}
};

} // namespace details

inline constexpr auto Upper = details::ToUpperType{};
[[nodiscard]] TextWithEntities Bold(const QString &text);
[[nodiscard]] TextWithEntities Semibold(const QString &text);
[[nodiscard]] TextWithEntities Italic(const QString &text);
[[nodiscard]] TextWithEntities Link(
	const QString &text,
	const QString &url = u"internal:action"_q);
[[nodiscard]] TextWithEntities Link(const QString &text, int index);
[[nodiscard]] TextWithEntities Link(
	TextWithEntities text,
	const QString &url = u"internal:action"_q);
[[nodiscard]] TextWithEntities Link(TextWithEntities text, int index);
[[nodiscard]] TextWithEntities Colorized(
	const QString &text,
	int index = 0);
[[nodiscard]] TextWithEntities Colorized(
	TextWithEntities text,
	int index = 0);
[[nodiscard]] TextWithEntities Wrapped(
	TextWithEntities text,
	EntityType type,
	const QString &data = QString());
[[nodiscard]] TextWithEntities RichLangValue(const QString &text);
[[nodiscard]] inline TextWithEntities WithEntities(const QString &text) {
	return { text };
}

[[nodiscard]] TextWithEntities SingleCustomEmoji(QString data);

[[nodiscard]] inline auto ToUpper() {
	return rpl::map(Upper);
}

[[nodiscard]] inline auto ToBold() {
	return rpl::map(Bold);
}

[[nodiscard]] inline auto ToSemibold() {
	return rpl::map(Semibold);
}

[[nodiscard]] inline auto ToItalic() {
	return rpl::map(Italic);
}

[[nodiscard]] inline auto ToLink(const QString &url = "internal:action") {
	return rpl::map([=](const auto &text) {
		return Link(text, url);
	});
}

[[nodiscard]] inline auto ToRichLangValue() {
	return rpl::map(RichLangValue);
}

[[nodiscard]] inline auto ToWithEntities() {
	return rpl::map(WithEntities);
}

[[nodiscard]] TextWithEntities Mid(
	const TextWithEntities &text,
	int position,
	int n = -1);
[[nodiscard]] TextWithEntities Filtered(
	const TextWithEntities &result,
	const std::vector<EntityType> &types);

} // namespace Text
} // namespace Ui
