// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_utilities.h"

#include "base/algorithm.h"
#include "base/qt_adapters.h"

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

} // namespace Text
} // namespace Ui
