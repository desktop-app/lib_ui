// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text_entity.h"

namespace style {
struct IconEmoji;
} // namespace style

namespace Ui::Text {

class CustomEmoji;

[[nodiscard]] TextWithEntities Bold(const QString &text);
[[nodiscard]] TextWithEntities Semibold(const QString &text);
[[nodiscard]] TextWithEntities Italic(const QString &text);
[[nodiscard]] TextWithEntities Underline(const QString &text);
[[nodiscard]] TextWithEntities StrikeOut(const QString &text);
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

[[nodiscard]] TextWithEntities SingleCustomEmoji(
	QString data,
	QString text = QString());

[[nodiscard]] TextWithEntities IconEmoji(
	not_null<const style::IconEmoji*> emoji,
	QString text = QString());

[[nodiscard]] std::unique_ptr<CustomEmoji> TryMakeSimpleEmoji(
	QStringView data);

[[nodiscard]] TextWithEntities Mid(
	const TextWithEntities &text,
	int position,
	int n = -1);
[[nodiscard]] TextWithEntities Filtered(
	const TextWithEntities &result,
	const std::vector<EntityType> &types);

[[nodiscard]] QString FixAmpersandInAction(QString text);

[[nodiscard]] TextWithEntities WrapEmailPattern(const QString &);

[[nodiscard]] QList<QStringView> Words(QStringView lower);

[[nodiscard]] QString StripUrlProtocol(const QString &link);

} // namespace Ui::Text
