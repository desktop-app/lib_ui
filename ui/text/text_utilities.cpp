// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_utilities.h"

#include "base/algorithm.h"
#include "base/qt/qt_string_view.h"

#include <QtCore/QRegularExpression>

namespace Ui {
namespace Text {
namespace {

TextWithEntities WithSingleEntity(
		const QString &text,
		EntityType type,
		const QString &data = QString()) {
	auto result = TextWithEntities{ text };
	result.entities.push_back({ type, 0, int(text.size()), data });
	return result;
}

} // namespace

TextWithEntities Bold(const QString &text) {
	return WithSingleEntity(text, EntityType::Bold);
}

TextWithEntities Semibold(const QString &text) {
	return WithSingleEntity(text, EntityType::Semibold);
}

TextWithEntities Italic(const QString &text) {
	return WithSingleEntity(text, EntityType::Italic);
}

TextWithEntities Link(const QString &text, const QString &url) {
	return WithSingleEntity(text, EntityType::CustomUrl, url);
}

TextWithEntities Link(const QString &text, int index) {
	Expects(index > 0 && index < 10);
	return Link(text, QString("internal:index") + QChar('0' + index));
}

TextWithEntities PlainLink(const QString &text) {
	return WithSingleEntity(text, EntityType::PlainLink);
}

TextWithEntities Wrapped(
		TextWithEntities text,
		EntityType type,
		const QString &data) {
	text.entities.insert(
		text.entities.begin(),
		{ type, 0, int(text.text.size()), data });
	return text;
}

TextWithEntities RichLangValue(const QString &text) {
	static const auto kStart = QRegularExpression("(\\*\\*|__)");

	auto result = TextWithEntities();
	auto offset = 0;
	while (offset < text.size()) {
		const auto m = kStart.match(text, offset);
		if (!m.hasMatch()) {
			result.text.append(base::StringViewMid(text, offset));
			break;
		}
		const auto position = m.capturedStart();
		const auto from = m.capturedEnd();
		const auto tag = m.capturedView();
		const auto till = text.indexOf(tag, from + 1);
		if (till <= from) {
			offset = from;
			continue;
		}
		if (position > offset) {
			result.text.append(base::StringViewMid(text, offset, position - offset));
		}
		const auto type = (tag == qstr("__"))
			? EntityType::Italic
			: EntityType::Bold;
		result.entities.push_back({ type, int(result.text.size()), int(till - from) });
		result.text.append(base::StringViewMid(text, from, till - from));
		offset = till + tag.size();
	}
	return result;
}

TextWithEntities Mid(const TextWithEntities &text, int position, int n) {
	if (n == -1) {
		n = int(text.text.size()) - position;
	}
	const auto midEnd = (position + n);
	auto entities = ranges::views::all(
		text.entities
	) | ranges::views::filter([&](const EntityInText &entity) {
		// Intersects of ranges.
		const auto l1 = entity.offset();
		const auto r1 = entity.offset() + entity.length() - 1;
		const auto l2 = position;
		const auto r2 = midEnd - 1;
		return !(l1 > r2 || l2 > r1);
	}) | ranges::views::transform([&](const EntityInText &entity) {
		if ((entity.offset() == position) && (entity.length() == n)) {
			return entity;
		}
		const auto start = std::max(entity.offset(), position);
		const auto end = std::min(entity.offset() + entity.length(), midEnd);
		return EntityInText(
			entity.type(),
			start - position,
			end - start,
			entity.data());
	}) | ranges::to<EntitiesInText>();
	return {
		.text = text.text.mid(position, n),
		.entities = std::move(entities),
	};
}

TextWithEntities Filtered(
		const TextWithEntities &text,
		const std::vector<EntityType> &types) {
	auto result = ranges::views::all(
		text.entities
	) | ranges::views::filter([&](const EntityInText &entity) {
		return ranges::contains(types, entity.type());
	}) | ranges::to<EntitiesInText>();
	return { .text = text.text, .entities = std::move(result) };
}

} // namespace Text
} // namespace Ui
