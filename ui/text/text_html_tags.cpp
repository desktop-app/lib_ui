// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_html_tags.h"

#include "ui/widgets/fields/input_field.h"
#include "ui/basic_click_handlers.h"

#include <QtCore/QHash>
#include <QtGui/QTextDocumentFragment>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace TextUtilities {
namespace {

enum class HtmlTag {
	Pre,
	Code,
	Blockquote,
	Link,
	Bold,
	Italic,
	Underline,
	StrikeOut,
};

using NamedEntityCache = QHash<QString, std::optional<QString>>;

struct HtmlAttribute {
	QString name;
	QString value;
	bool hasValue = false;
};

struct ActiveTags {
	int pre = 0;
	int code = 0;
	int blockquote = 0;
	int bold = 0;
	int italic = 0;
	int underline = 0;
	int strikeOut = 0;
	std::vector<QString> links;
};

struct OpenAnchor {
	QString href;
	int visibleOffset = -1;
};

struct ParseState {
	TextWithTags result;
	TextWithTags::Tags tags;
	ActiveTags active;
	std::vector<OpenAnchor> openAnchors;
	NamedEntityCache entityCache;
	std::vector<QString> hidden;
	int trailingStructuralNewlines = 0;
	bool pendingWhitespace = false;
	QString pendingWhitespaceTagId;
	bool removedRedundantLinks = false;
};

struct HtmlTagCounts {
	int pre = 0;
	int code = 0;
	int blockquote = 0;
	int bold = 0;
	int italic = 0;
	int underline = 0;
	int strikeOut = 0;
	std::vector<QString> links;
};

struct HtmlTagDescriptor {
	HtmlTag tag = HtmlTag::Bold;
	QString data;

	[[nodiscard]] bool operator==(const HtmlTagDescriptor &other) const {
		return tag == other.tag && data == other.data;
	}
};

struct HtmlTagEvent {
	int offset = 0;
	HtmlTagDescriptor descriptor;
	int delta = 0;
};

struct LinkRun {
	int from = 0;
	int till = 0;
	QString link;
	bool suppress = false;
};

[[nodiscard]] int HtmlTagOrder(HtmlTag tag) {
	switch (tag) {
	case HtmlTag::Blockquote: return 0;
	case HtmlTag::Link: return 1;
	case HtmlTag::Bold: return 2;
	case HtmlTag::Italic: return 3;
	case HtmlTag::Underline: return 4;
	case HtmlTag::StrikeOut: return 5;
	case HtmlTag::Pre: return 6;
	case HtmlTag::Code: return 7;
	}
	return 0;
}

[[nodiscard]] bool HtmlTagEventLess(
		const HtmlTagEvent &a,
		const HtmlTagEvent &b) {
	if (a.offset != b.offset) {
		return a.offset < b.offset;
	}
	if (a.delta != b.delta) {
		return a.delta < b.delta;
	}
	const auto aOrder = HtmlTagOrder(a.descriptor.tag);
	const auto bOrder = HtmlTagOrder(b.descriptor.tag);
	if (aOrder != bOrder) {
		return aOrder < bOrder;
	}
	return a.descriptor.data < b.descriptor.data;
}

void AddUnique(
		std::vector<HtmlTagDescriptor> &descriptors,
		HtmlTagDescriptor descriptor) {
	const auto i = std::find(
		descriptors.begin(),
		descriptors.end(),
		descriptor);
	if (i != descriptors.end()) {
		return;
	}
	descriptors.push_back(std::move(descriptor));
}

[[nodiscard]] bool IsPreTag(QStringView tag) {
	return tag.startsWith(Ui::InputField::kTagPre);
}

[[nodiscard]] bool IsSupportedLinkTag(QStringView tag) {
	return Ui::InputField::IsValidMarkdownLink(tag)
		&& !TextUtilities::IsMentionLink(tag);
}

[[nodiscard]] QString SupportedLink(QStringView id) {
	auto result = QString();
	for (const auto &tag : TextUtilities::SplitTags(id)) {
		if (IsSupportedLinkTag(tag)) {
			result = tag.toString();
		}
	}
	return result;
}

[[nodiscard]] QString VisibleEntityText(
		const TextWithEntities &text,
		const EntityInText &entity) {
	const auto textSize = int(text.text.size());
	const auto from = std::clamp(entity.offset(), 0, textSize);
	const auto till = std::clamp(entity.offset() + entity.length(), 0, textSize);
	return (till > from)
		? QStringView(text.text).mid(from, till - from).toString()
		: QString();
}

[[nodiscard]] EntitiesInText HtmlExportEntities(const TextForMimeData &text) {
	auto result = EntitiesInText();
	result.reserve(text.rich.entities.size());
	for (const auto &original : text.rich.entities) {
		auto entity = original;
		const auto offset = entity.offset();
		const auto length = entity.length();
		switch (entity.type()) {
		case EntityType::CustomEmoji:
		case EntityType::FormattedDate:
			continue;
		case EntityType::Url: {
			const auto url = entity.data().isEmpty()
				? VisibleEntityText(text.rich, entity)
				: entity.data();
			entity = EntityInText(
				EntityType::CustomUrl,
				offset,
				length,
				UrlClickHandler::EncodeForOpening(url));
		} break;
		case EntityType::Email: {
			const auto email = entity.data().isEmpty()
				? VisibleEntityText(text.rich, entity)
				: entity.data();
			entity = EntityInText(
				EntityType::CustomUrl,
				offset,
				length,
				email);
		} break;
		default: break;
		}
		result.push_back(std::move(entity));
	}
	return result;
}

[[nodiscard]] bool HasTag(
		const std::vector<HtmlTagDescriptor> &descriptors,
		HtmlTag tag) {
	return std::any_of(
		descriptors.begin(),
		descriptors.end(),
		[=](const auto &descriptor) {
			return descriptor.tag == tag;
		});
}

[[nodiscard]] std::vector<HtmlTagDescriptor> SupportedTags(
		QStringView id,
		bool suppressLink) {
	auto result = std::vector<HtmlTagDescriptor>();
	auto hasPre = false;
	auto hasCode = false;
	auto link = QString();
	for (const auto &tag : TextUtilities::SplitTags(id)) {
		if (tag == Ui::InputField::kTagBold) {
			AddUnique(result, { HtmlTag::Bold });
		} else if (tag == Ui::InputField::kTagItalic) {
			AddUnique(result, { HtmlTag::Italic });
		} else if (tag == Ui::InputField::kTagUnderline) {
			AddUnique(result, { HtmlTag::Underline });
		} else if (tag == Ui::InputField::kTagStrikeOut) {
			AddUnique(result, { HtmlTag::StrikeOut });
		} else if (tag == Ui::InputField::kTagCode) {
			hasCode = true;
		} else if (IsPreTag(tag)) {
			hasPre = true;
		} else if (tag == Ui::InputField::kTagBlockquote
			|| tag == Ui::InputField::kTagBlockquoteCollapsed) {
			AddUnique(result, { HtmlTag::Blockquote });
		} else if (IsSupportedLinkTag(tag)) {
			link = tag.toString();
		}
	}
	if (hasPre) {
		return { { HtmlTag::Pre } };
	}
	if (hasCode) {
		return { { HtmlTag::Code } };
	}
	if (!link.isEmpty() && !suppressLink) {
		AddUnique(result, { HtmlTag::Link, link });
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
		return HtmlTagOrder(a.tag) < HtmlTagOrder(b.tag);
	});
	return result;
}

void UpdateCount(
		HtmlTagCounts &counts,
		const HtmlTagDescriptor &descriptor,
		int delta) {
	if (descriptor.tag == HtmlTag::Link) {
		if (delta > 0) {
			counts.links.push_back(descriptor.data);
		} else if (delta < 0) {
			for (auto i = counts.links.end(); i != counts.links.begin();) {
				--i;
				if (*i == descriptor.data) {
					counts.links.erase(i);
					break;
				}
			}
		}
		return;
	}
	switch (descriptor.tag) {
	case HtmlTag::Pre: counts.pre += delta; break;
	case HtmlTag::Code: counts.code += delta; break;
	case HtmlTag::Blockquote: counts.blockquote += delta; break;
	case HtmlTag::Bold: counts.bold += delta; break;
	case HtmlTag::Italic: counts.italic += delta; break;
	case HtmlTag::Underline: counts.underline += delta; break;
	case HtmlTag::StrikeOut: counts.strikeOut += delta; break;
	case HtmlTag::Link: break;
	}
}

[[nodiscard]] QString ActiveLink(const HtmlTagCounts &counts) {
	return counts.links.empty() ? QString() : counts.links.back();
}

void AddLinkRunSegment(
		std::vector<LinkRun> &runs,
		int from,
		int till,
		const QString &link) {
	if (link.isEmpty() || till <= from) {
		return;
	}
	if (!runs.empty()
		&& runs.back().till == from
		&& runs.back().link == link) {
		runs.back().till = till;
	} else {
		runs.push_back({ from, till, link });
	}
}

[[nodiscard]] std::vector<LinkRun> LinkRuns(const TextWithTags &text) {
	const auto textSize = int(text.text.size());
	auto events = std::vector<HtmlTagEvent>();
	events.reserve(2 * text.tags.size());
	for (const auto &tag : text.tags) {
		const auto link = SupportedLink(tag.id);
		if (link.isEmpty()) {
			continue;
		}
		const auto from = std::clamp(tag.offset, 0, textSize);
		const auto till = std::clamp(tag.offset + tag.length, 0, textSize);
		if (till <= from) {
			continue;
		}
		const auto descriptor = HtmlTagDescriptor{ HtmlTag::Link, link };
		events.push_back({ from, descriptor, 1 });
		events.push_back({ till, descriptor, -1 });
	}
	if (events.empty()) {
		return {};
	}
	std::sort(events.begin(), events.end(), HtmlTagEventLess);

	auto result = std::vector<LinkRun>();
	auto counts = HtmlTagCounts();
	auto position = 0;
	for (auto i = size_t(0), size = events.size(); i != size;) {
		const auto offset = events[i].offset;
		AddLinkRunSegment(result, position, offset, ActiveLink(counts));
		do {
			UpdateCount(counts, events[i].descriptor, events[i].delta);
			++i;
		} while (i != size && events[i].offset == offset);
		position = offset;
	}
	AddLinkRunSegment(result, position, textSize, ActiveLink(counts));
	for (auto &run : result) {
		const auto visible = QStringView(text.text).mid(
			run.from,
			run.till - run.from);
		run.suppress = (run.link
			== UrlClickHandler::EncodeForOpening(visible.toString()));
	}
	return result;
}

[[nodiscard]] bool IsSuppressedLinkRun(
		const std::vector<LinkRun> &runs,
		int from,
		int till,
		const QString &link) {
	const auto i = std::upper_bound(
		runs.begin(),
		runs.end(),
		from,
		[](int offset, const LinkRun &run) {
			return offset < run.from;
		});
	if (i == runs.begin()) {
		return false;
	}
	const auto &run = *(i - 1);
	return run.suppress
		&& run.link == link
		&& run.from <= from
		&& till <= run.till;
}

[[nodiscard]] std::vector<HtmlTagDescriptor> ActiveHtmlTags(
		const HtmlTagCounts &counts) {
	auto result = std::vector<HtmlTagDescriptor>();
	result.reserve(7);
	if (counts.pre > 0) {
		return { { HtmlTag::Pre } };
	}
	if (counts.code > 0) {
		return { { HtmlTag::Code } };
	}
	if (counts.blockquote > 0) {
		result.push_back({ HtmlTag::Blockquote });
	}
	if (!counts.links.empty()) {
		result.push_back({ HtmlTag::Link, counts.links.back() });
	}
	if (counts.bold > 0) {
		result.push_back({ HtmlTag::Bold });
	}
	if (counts.italic > 0) {
		result.push_back({ HtmlTag::Italic });
	}
	if (counts.underline > 0) {
		result.push_back({ HtmlTag::Underline });
	}
	if (counts.strikeOut > 0) {
		result.push_back({ HtmlTag::StrikeOut });
	}
	return result;
}

[[nodiscard]] QString EscapeHtmlAttribute(QStringView value) {
	auto result = QString();
	result.reserve(value.size());
	for (const auto ch : value) {
		if (ch == '&') {
			result.append(u"&amp;"_q);
		} else if (ch == '"') {
			result.append(u"&quot;"_q);
		} else if (ch == '<') {
			result.append(u"&lt;"_q);
		} else if (ch == '>') {
			result.append(u"&gt;"_q);
		} else {
			result.append(ch);
		}
	}
	return result;
}

[[nodiscard]] QString SerializeLinkHref(QStringView link) {
	if (link.indexOf(':') < 0) {
		const auto value = link.toString();
		if (UrlClickHandler::IsEmail(value)) {
			return u"mailto:%1"_q.arg(value);
		}
	}
	return link.toString();
}

void AppendOpenTag(QString &result, const HtmlTagDescriptor &descriptor) {
	switch (descriptor.tag) {
	case HtmlTag::Pre: result.append(u"<pre>"_q); break;
	case HtmlTag::Code: result.append(u"<code>"_q); break;
	case HtmlTag::Blockquote:
		result.append(u"<blockquote>"_q);
		break;
	case HtmlTag::Bold: result.append(u"<b>"_q); break;
	case HtmlTag::Italic: result.append(u"<i>"_q); break;
	case HtmlTag::Underline: result.append(u"<u>"_q); break;
	case HtmlTag::StrikeOut: result.append(u"<s>"_q); break;
	case HtmlTag::Link:
		result.append(u"<a href=\""_q);
		result.append(EscapeHtmlAttribute(SerializeLinkHref(descriptor.data)));
		result.append(u"\">"_q);
		break;
	}
}

void AppendCloseTag(QString &result, HtmlTag tag) {
	switch (tag) {
	case HtmlTag::Pre: result.append(u"</pre>"_q); break;
	case HtmlTag::Code: result.append(u"</code>"_q); break;
	case HtmlTag::Blockquote:
		result.append(u"</blockquote>"_q);
		break;
	case HtmlTag::Bold: result.append(u"</b>"_q); break;
	case HtmlTag::Italic: result.append(u"</i>"_q); break;
	case HtmlTag::Underline: result.append(u"</u>"_q); break;
	case HtmlTag::StrikeOut: result.append(u"</s>"_q); break;
	case HtmlTag::Link: result.append(u"</a>"_q); break;
	}
}

void SwitchTags(
		QString &result,
		const std::vector<HtmlTagDescriptor> &current,
		const std::vector<HtmlTagDescriptor> &next) {
	auto shared = 0;
	const auto currentSize = int(current.size());
	const auto nextSize = int(next.size());
	while (shared != currentSize
		&& shared != nextSize
		&& current[shared] == next[shared]) {
		++shared;
	}
	for (auto i = currentSize; i != shared; --i) {
		AppendCloseTag(result, current[i - 1].tag);
	}
	for (auto i = shared; i != nextSize; ++i) {
		AppendOpenTag(result, next[i]);
	}
}

void AppendEscaped(QString &result, QStringView text, bool preserveNewlines) {
	auto start = 0;
	const auto size = text.size();
	for (auto i = 0; i != size; ++i) {
		const auto ch = text[i];
		if (ch != '\r' && ch != '\n') {
			continue;
		}
		result.append(text.mid(start, i - start).toString().toHtmlEscaped());
		if (preserveNewlines) {
			result.append('\n');
		} else {
			result.append(u"<br>"_q);
		}
		if (ch == '\r' && i + 1 != size && text[i + 1] == '\n') {
			++i;
		}
		start = i + 1;
	}
	result.append(text.mid(start).toString().toHtmlEscaped());
}

[[nodiscard]] bool IsTagNameStartChar(QChar ch) {
	return ch.isLetter();
}

[[nodiscard]] bool IsTagNameChar(QChar ch) {
	return ch.isLetterOrNumber();
}

[[nodiscard]] bool IsAttributeNameStartChar(QChar ch) {
	return (ch.isLetterOrNumber()
		|| ch == '-'
		|| ch == '_'
		|| ch == ':');
}

[[nodiscard]] bool IsAttributeNameChar(QChar ch) {
	return IsAttributeNameStartChar(ch);
}

[[nodiscard]] int FindSequence(
		QStringView text,
		int from,
		const QString &needle) {
	const auto size = text.size();
	const auto needleSize = needle.size();
	if (!needleSize || needleSize > size) {
		return -1;
	}
	for (auto i = from; i + needleSize <= size; ++i) {
		auto matches = true;
		for (auto j = 0; j != needleSize; ++j) {
			if (text[i + j] != needle[j]) {
				matches = false;
				break;
			}
		}
		if (matches) {
			return i;
		}
	}
	return -1;
}

[[nodiscard]] bool HasSequence(
		QStringView text,
		int from,
		const QString &needle) {
	const auto size = text.size();
	const auto needleSize = needle.size();
	if (from < 0 || needleSize > size - from) {
		return false;
	}
	for (auto i = 0; i != needleSize; ++i) {
		if (text[from + i] != needle[i]) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] int FindTagEnd(QStringView html, int from) {
	auto quote = QChar();
	for (auto i = from, size = int(html.size()); i != size; ++i) {
		const auto ch = html[i];
		if (!quote.isNull()) {
			if (ch == quote) {
				quote = QChar();
			}
		} else if (ch == '\'' || ch == '"') {
			quote = ch;
		} else if (ch == '>') {
			return i;
		}
	}
	return -1;
}

[[nodiscard]] bool IsSelfClosing(QStringView html, int from, int till) {
	for (auto i = till - 1; i > from; --i) {
		const auto ch = html[i];
		if (ch.isSpace()) {
			continue;
		}
		return (ch == '/');
	}
	return false;
}

[[nodiscard]] QString ReadTagName(
		QStringView html,
		int from,
		int till,
		int *nameEnd) {
	while (from != till && html[from].isSpace()) {
		++from;
	}
	if (from == till || !IsTagNameStartChar(html[from])) {
		*nameEnd = from;
		return QString();
	}
	const auto start = from;
	while (from != till && IsTagNameChar(html[from])) {
		++from;
	}
	*nameEnd = from;
	return html.mid(start, from - start).toString().toLower();
}

[[nodiscard]] std::optional<QString> DecodeNumericEntity(QStringView entity) {
	if (entity.isEmpty() || entity[0] != '#') {
		return std::nullopt;
	}
	auto number = entity.mid(1);
	auto base = 10;
	if (!number.isEmpty() && (number[0] == 'x' || number[0] == 'X')) {
		number = number.mid(1);
		base = 16;
	}
	auto ok = false;
	const auto code = number.toString().toUInt(&ok, base);
	if (!ok
		|| !code
		|| code > 0x10FFFF
		|| (code >= 0xD800 && code <= 0xDFFF)) {
		return std::nullopt;
	}
	const auto codepoint = char32_t(code);
	return QString::fromUcs4(&codepoint, 1);
}

[[nodiscard]] std::optional<QString> DecodeCommonNamedEntity(
		QStringView entity) {
	if (entity == u"amp"_q) {
		return u"&"_q;
	} else if (entity == u"lt"_q) {
		return u"<"_q;
	} else if (entity == u"gt"_q) {
		return u">"_q;
	} else if (entity == u"quot"_q) {
		return u"\""_q;
	} else if (entity == u"apos"_q) {
		return u"'"_q;
	} else if (entity == u"nbsp"_q) {
		return QString(QChar(0x00A0));
	} else if (entity == u"ndash"_q) {
		return u"\u2013"_q;
	} else if (entity == u"mdash"_q) {
		return u"\u2014"_q;
	} else if (entity == u"hellip"_q) {
		return u"\u2026"_q;
	} else if (entity == u"lsquo"_q) {
		return u"\u2018"_q;
	} else if (entity == u"rsquo"_q) {
		return u"\u2019"_q;
	} else if (entity == u"ldquo"_q) {
		return u"\u201C"_q;
	} else if (entity == u"rdquo"_q) {
		return u"\u201D"_q;
	} else if (entity == u"copy"_q) {
		return u"\u00A9"_q;
	} else if (entity == u"reg"_q) {
		return u"\u00AE"_q;
	} else if (entity == u"trade"_q) {
		return u"\u2122"_q;
	} else if (entity == u"bull"_q) {
		return u"\u2022"_q;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<QString> DecodeNamedEntity(
		QStringView entity,
		NamedEntityCache &cache) {
	if (const auto common = DecodeCommonNamedEntity(entity)) {
		return common;
	}
	const auto key = entity.toString();
	const auto cached = cache.constFind(key);
	if (cached != cache.cend()) {
		return cached.value();
	}
	const auto source = u"&"_q
		+ key
		+ u";"_q;
	const auto decoded = QTextDocumentFragment::fromHtml(source).toPlainText();
	const auto result = (decoded.isEmpty() || decoded == source)
		? std::optional<QString>()
		: std::make_optional(decoded);
	cache.insert(key, result);
	return result;
}

[[nodiscard]] QString DecodeEntities(
		QStringView text,
		NamedEntityCache &cache) {
	auto result = QString();
	result.reserve(text.size());
	for (auto i = 0, size = int(text.size()); i != size;) {
		if (text[i] != '&') {
			result.append(text[i++]);
			continue;
		}
		auto semicolon = i + 1;
		while (semicolon != size
			&& semicolon - i <= 32
			&& text[semicolon] != ';') {
			++semicolon;
		}
		if (semicolon == size || text[semicolon] != ';') {
			result.append(text[i++]);
			continue;
		}
		const auto entity = text.mid(i + 1, semicolon - i - 1);
		const auto decoded = DecodeNumericEntity(entity);
		auto named = std::optional<QString>();
		if (!decoded) {
			named = DecodeNamedEntity(entity, cache);
		}
		if (decoded) {
			result.append(*decoded);
			i = semicolon + 1;
		} else if (named) {
			result.append(*named);
			i = semicolon + 1;
		} else {
			result.append(text.mid(i, semicolon - i + 1).toString());
			i = semicolon + 1;
		}
	}
	return result;
}

[[nodiscard]] std::vector<HtmlAttribute> ReadAttributes(
		QStringView html,
		int from,
		int till,
		NamedEntityCache &cache) {
	while (from != till && html[till - 1].isSpace()) {
		--till;
	}
	if (from != till && html[till - 1] == '/') {
		--till;
	}
	auto result = std::vector<HtmlAttribute>();
	while (from != till) {
		while (from != till && html[from].isSpace()) {
			++from;
		}
		if (from == till) {
			break;
		}
		if (!IsAttributeNameStartChar(html[from])) {
			++from;
			continue;
		}
		const auto nameStart = from;
		do {
			++from;
		} while (from != till && IsAttributeNameChar(html[from]));

		auto attribute = HtmlAttribute();
		attribute.name = html.mid(nameStart, from - nameStart).toString().toLower();
		while (from != till && html[from].isSpace()) {
			++from;
		}
		if (from == till || html[from] != '=') {
			result.push_back(std::move(attribute));
			continue;
		}
		attribute.hasValue = true;
		++from;
		while (from != till && html[from].isSpace()) {
			++from;
		}
		if (from != till && (html[from] == '"' || html[from] == '\'')) {
			const auto quote = html[from++];
			const auto valueStart = from;
			while (from != till && html[from] != quote) {
				++from;
			}
			attribute.value = DecodeEntities(
				html.mid(valueStart, from - valueStart),
				cache);
			if (from != till) {
				++from;
			}
		} else {
			const auto valueStart = from;
			while (from != till && !html[from].isSpace()) {
				++from;
			}
			attribute.value = DecodeEntities(
				html.mid(valueStart, from - valueStart),
				cache);
		}
		result.push_back(std::move(attribute));
	}
	return result;
}

[[nodiscard]] std::optional<QString> AttributeValue(
		const std::vector<HtmlAttribute> &attributes,
		QStringView name) {
	for (const auto &attribute : attributes) {
		if (attribute.name == name && attribute.hasValue) {
			return attribute.value;
		}
	}
	return std::nullopt;
}

[[nodiscard]] bool HasAttribute(
		const std::vector<HtmlAttribute> &attributes,
		QStringView name) {
	return std::any_of(
		attributes.begin(),
		attributes.end(),
		[=](const auto &attribute) {
			return attribute.name == name;
		});
}

[[nodiscard]] QString NormalizedStyleValue(QStringView value) {
	auto result = QString();
	result.reserve(value.size());
	for (const auto ch : value) {
		if (!ch.isSpace()) {
			result.append(ch.toLower());
		}
	}
	return result;
}

[[nodiscard]] bool IsHiddenByAttributes(
		const std::vector<HtmlAttribute> &attributes) {
	if (HasAttribute(attributes, u"hidden"_q)) {
		return true;
	}
	if (const auto value = AttributeValue(attributes, u"aria-hidden"_q)) {
		if (value->trimmed().toLower() == u"true"_q) {
			return true;
		}
	}
	if (const auto value = AttributeValue(attributes, u"style"_q)) {
		const auto normalized = NormalizedStyleValue(*value);
		return (normalized.contains(u"display:none"_q)
			|| normalized.contains(u"visibility:hidden"_q));
	}
	return false;
}

[[nodiscard]] QString ValidatedHref(
		const std::vector<HtmlAttribute> &attributes) {
	const auto value = AttributeValue(attributes, u"href"_q);
	if (!value) {
		return QString();
	}
	const auto href = value->trimmed();
	return (Ui::InputField::IsValidMarkdownLink(href)
		&& !TextUtilities::IsMentionLink(href))
		? href
		: QString();
}

[[nodiscard]] QString NormalizeNewlines(QString text) {
	text.replace(u"\r\n"_q, u"\n"_q);
	text.replace('\r', '\n');
	return text;
}

[[nodiscard]] bool IsPreActive(const ActiveTags &active) {
	return (active.pre > 0);
}

[[nodiscard]] bool IsCollapsibleSpace(QChar ch) {
	return (ch == ' '
		|| ch == '\t'
		|| ch == '\n'
		|| ch == '\r'
		|| ch == '\f');
}

[[nodiscard]] QString ActiveTagId(const ActiveTags &active) {
	if (active.pre > 0) {
		return Ui::InputField::kTagPre;
	}
	if (active.code > 0) {
		return Ui::InputField::kTagCode;
	}
	auto tags = QList<QStringView>();
	if (active.blockquote > 0) {
		tags.push_back(Ui::InputField::kTagBlockquote);
	}
	for (auto i = active.links.crbegin(); i != active.links.crend(); ++i) {
		if (!i->isEmpty()) {
			tags.push_back(QStringView(*i));
			break;
		}
	}
	if (active.bold > 0) {
		tags.push_back(Ui::InputField::kTagBold);
	}
	if (active.italic > 0) {
		tags.push_back(Ui::InputField::kTagItalic);
	}
	if (active.underline > 0) {
		tags.push_back(Ui::InputField::kTagUnderline);
	}
	if (active.strikeOut > 0) {
		tags.push_back(Ui::InputField::kTagStrikeOut);
	}
	return TextUtilities::JoinTag(tags);
}

[[nodiscard]] TextWithTags::Tags SimplifyParserTags(TextWithTags::Tags tags) {
	auto result = TextWithTags::Tags();
	result.reserve(tags.size());
	for (const auto &tag : tags) {
		if (!result.isEmpty()
			&& result.back().offset + result.back().length == tag.offset
			&& result.back().id == tag.id) {
			result.back().length += tag.length;
		} else {
			result.push_back(tag);
		}
	}
	return result;
}

[[nodiscard]] bool TagIdContains(QStringView tagId, QStringView value) {
	return !value.isEmpty()
		&& TextUtilities::SplitTags(tagId).contains(value);
}

void RememberAnchorVisibleOffset(ParseState &state, const QString &tagId) {
	if (state.openAnchors.empty()) {
		return;
	}
	auto &anchor = state.openAnchors.back();
	if (anchor.visibleOffset < 0 && TagIdContains(tagId, anchor.href)) {
		anchor.visibleOffset = state.result.text.size();
	}
}

void RemoveRedundantAnchorLink(ParseState &state, const OpenAnchor &anchor) {
	if (anchor.href.isEmpty() || anchor.visibleOffset < 0) {
		return;
	}
	const auto offset = anchor.visibleOffset;
	const auto till = state.result.text.size();
	if (till <= offset) {
		return;
	}
	const auto visible = QStringView(state.result.text).mid(offset, till - offset);
	if (anchor.href != UrlClickHandler::EncodeForOpening(visible.toString())) {
		return;
	}
	auto removed = false;
	for (auto i = state.tags.begin(); i != state.tags.end();) {
		const auto tagTill = i->offset + i->length;
		if (i->offset < offset || tagTill > till) {
			++i;
			continue;
		}
		const auto updated = TextUtilities::TagWithRemoved(i->id, anchor.href);
		if (updated == i->id) {
			++i;
			continue;
		}
		removed = true;
		if (updated.isEmpty()) {
			i = state.tags.erase(i);
		} else {
			i->id = updated;
			++i;
		}
	}
	if (state.pendingWhitespace
		&& TagIdContains(state.pendingWhitespaceTagId, anchor.href)) {
		const auto updated = TextUtilities::TagWithRemoved(
			state.pendingWhitespaceTagId,
			anchor.href);
		if (updated != state.pendingWhitespaceTagId) {
			state.pendingWhitespaceTagId = updated;
			removed = true;
		}
	}
	if (removed) {
		state.removedRedundantLinks = true;
	}
}

void AppendTaggedText(
		ParseState &state,
		QStringView text,
		const QString &tagId) {
	if (text.isEmpty()) {
		return;
	}
	RememberAnchorVisibleOffset(state, tagId);
	const auto offset = int(state.result.text.size());
	state.result.text.append(text);
	if (!tagId.isEmpty()) {
		state.tags.push_back({ offset, int(text.size()), tagId });
	}
	state.trailingStructuralNewlines = 0;
}

void ClearPendingWhitespace(ParseState &state) {
	state.pendingWhitespace = false;
	state.pendingWhitespaceTagId.clear();
}

void AddPendingWhitespace(ParseState &state, const QString &tagId) {
	if (state.result.text.isEmpty()
		|| state.result.text.back() == '\n') {
		return;
	}
	state.pendingWhitespace = true;
	state.pendingWhitespaceTagId = tagId;
}

void FlushPendingWhitespace(ParseState &state) {
	if (!state.pendingWhitespace) {
		return;
	}
	if (!state.result.text.isEmpty()
		&& state.result.text.back() != '\n') {
		const auto space = QString(QChar(' '));
		AppendTaggedText(
			state,
			space,
			state.pendingWhitespaceTagId);
	}
	ClearPendingWhitespace(state);
}

void AppendText(ParseState &state, QString text) {
	text = NormalizeNewlines(std::move(text));
	if (text.isEmpty()) {
		return;
	}
	const auto tagId = ActiveTagId(state.active);
	if (IsPreActive(state.active)) {
		ClearPendingWhitespace(state);
		AppendTaggedText(state, text, tagId);
		return;
	}
	auto start = 0;
	for (auto i = 0, size = int(text.size()); i != size;) {
		if (!IsCollapsibleSpace(text[i])) {
			++i;
			continue;
		}
		if (start != i) {
			FlushPendingWhitespace(state);
			AppendTaggedText(
				state,
				QStringView(text).mid(start, i - start),
				tagId);
		}
		while (i != size && IsCollapsibleSpace(text[i])) {
			++i;
		}
		AddPendingWhitespace(state, tagId);
		start = i;
	}
	if (start != text.size()) {
		FlushPendingWhitespace(state);
		AppendTaggedText(
			state,
			QStringView(text).mid(start),
			tagId);
	}
}

void AppendText(ParseState &state, QStringView text) {
	AppendText(state, DecodeEntities(text, state.entityCache));
}

enum class LineBreakKind {
	Visible,
	Structural,
};

void AppendLine(ParseState &state, bool repeat, LineBreakKind kind) {
	ClearPendingWhitespace(state);
	if (!repeat
		&& (state.result.text.isEmpty()
			|| state.result.text.back() == '\n')) {
		return;
	}
	if (kind == LineBreakKind::Visible) {
		const auto newline = QString(QChar('\n'));
		AppendTaggedText(
			state,
			newline,
			ActiveTagId(state.active));
	} else {
		state.result.text.append(QChar('\n'));
		++state.trailingStructuralNewlines;
	}
}

[[nodiscard]] bool IsHiddenElement(const QString &name) {
	static const auto kElements = std::array{
		u"button"_q,
		u"head"_q,
		u"link"_q,
		u"meta"_q,
		u"noscript"_q,
		u"script"_q,
		u"select"_q,
		u"style"_q,
		u"svg"_q,
		u"template"_q,
		u"textarea"_q,
		u"title"_q,
	};
	for (const auto &element : kElements) {
		if (name == element) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool IsBlockBoundary(const QString &name) {
	static const auto kElements = std::array{
		u"address"_q,
		u"article"_q,
		u"aside"_q,
		u"blockquote"_q,
		u"dd"_q,
		u"div"_q,
		u"dl"_q,
		u"dt"_q,
		u"footer"_q,
		u"h1"_q,
		u"h2"_q,
		u"h3"_q,
		u"h4"_q,
		u"h5"_q,
		u"h6"_q,
		u"header"_q,
		u"li"_q,
		u"main"_q,
		u"ol"_q,
		u"p"_q,
		u"pre"_q,
		u"section"_q,
		u"table"_q,
		u"tbody"_q,
		u"td"_q,
		u"tfoot"_q,
		u"th"_q,
		u"thead"_q,
		u"tr"_q,
		u"ul"_q,
	};
	for (const auto &element : kElements) {
		if (name == element) {
			return true;
		}
	}
	return false;
}

template <std::size_t Size>
[[nodiscard]] bool NameIsOneOf(
		const QString &name,
		const std::array<QString, Size> &names) {
	for (const auto &entry : names) {
		if (name == entry) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] std::optional<HtmlTag> SupportedInputTag(const QString &name) {
	static const auto kBold = std::array{
		u"b"_q,
		u"strong"_q,
	};
	static const auto kItalic = std::array{
		u"i"_q,
		u"em"_q,
	};
	static const auto kStrikeOut = std::array{
		u"s"_q,
		u"strike"_q,
		u"del"_q,
	};
	if (NameIsOneOf(name, kBold)) {
		return HtmlTag::Bold;
	} else if (NameIsOneOf(name, kItalic)) {
		return HtmlTag::Italic;
	} else if (name == u"u"_q) {
		return HtmlTag::Underline;
	} else if (NameIsOneOf(name, kStrikeOut)) {
		return HtmlTag::StrikeOut;
	} else if (name == u"code"_q) {
		return HtmlTag::Code;
	} else if (name == u"pre"_q) {
		return HtmlTag::Pre;
	} else if (name == u"blockquote"_q) {
		return HtmlTag::Blockquote;
	}
	return std::nullopt;
}

void UpdateActive(ActiveTags &active, HtmlTag tag, bool closing) {
	auto update = [&](int &value) {
		if (closing) {
			if (value > 0) {
				--value;
			}
		} else {
			++value;
		}
	};
	switch (tag) {
	case HtmlTag::Pre: update(active.pre); break;
	case HtmlTag::Code: update(active.code); break;
	case HtmlTag::Blockquote: update(active.blockquote); break;
	case HtmlTag::Bold: update(active.bold); break;
	case HtmlTag::Italic: update(active.italic); break;
	case HtmlTag::Underline: update(active.underline); break;
	case HtmlTag::StrikeOut: update(active.strikeOut); break;
	case HtmlTag::Link: break;
	}
}

void CloseHiddenElement(ParseState &state, const QString &name) {
	for (auto i = state.hidden.size(); i != 0; --i) {
		if (state.hidden[i - 1] == name) {
			state.hidden.erase(
				state.hidden.begin() + i - 1,
				state.hidden.end());
			return;
		}
	}
}

void ProcessTag(
		ParseState &state,
		const QString &name,
		const std::vector<HtmlAttribute> &attributes,
		bool closing,
		bool selfClosing) {
	if (!state.hidden.empty()) {
		if (closing) {
			CloseHiddenElement(state, name);
		} else if (!selfClosing) {
			state.hidden.push_back(name);
		}
		return;
	}
	if (!closing
		&& !selfClosing
		&& (IsHiddenElement(name) || IsHiddenByAttributes(attributes))) {
		state.hidden.push_back(name);
		return;
	}
	if (!closing && name == u"br"_q) {
		AppendLine(state, true, LineBreakKind::Visible);
		return;
	}
	if (name == u"a"_q && !selfClosing) {
		if (closing) {
			if (!state.openAnchors.empty()) {
				RemoveRedundantAnchorLink(state, state.openAnchors.back());
				state.openAnchors.pop_back();
			}
			if (!state.active.links.empty()) {
				state.active.links.pop_back();
			}
		} else {
			const auto href = ValidatedHref(attributes);
			state.active.links.push_back(href);
			state.openAnchors.push_back({ href });
		}
		return;
	}
	const auto blockBoundary = IsBlockBoundary(name);
	if (!closing && blockBoundary) {
		AppendLine(state, false, LineBreakKind::Structural);
	}
	if (const auto tag = SupportedInputTag(name); tag && !selfClosing) {
		UpdateActive(state.active, *tag, closing);
	}
	if (closing && blockBoundary) {
		AppendLine(state, false, LineBreakKind::Structural);
	}
}

void TrimTrailingStructuralNewlines(ParseState &state) {
	if (state.trailingStructuralNewlines <= 0) {
		return;
	}
	auto &text = state.result;
	const auto length = text.text.size() - state.trailingStructuralNewlines;
	text.text.truncate(length);
	for (auto i = text.tags.begin(); i != text.tags.end();) {
		const auto till = i->offset + i->length;
		if (i->offset >= length) {
			i = text.tags.erase(i);
		} else {
			if (till > length) {
				i->length = length - i->offset;
			}
			++i;
		}
	}
	state.trailingStructuralNewlines = 0;
}

} // namespace

QString TextWithTagsToHtml(const TextWithTags &text) {
	if (text.text.isEmpty()) {
		return QString();
	}
	const auto textSize = int(text.text.size());
	const auto linkRuns = LinkRuns(text);
	auto events = std::vector<HtmlTagEvent>();
	events.reserve(4 * text.tags.size());
	for (const auto &tag : text.tags) {
		const auto from = std::clamp(tag.offset, 0, textSize);
		const auto till = std::clamp(tag.offset + tag.length, 0, textSize);
		if (till <= from) {
			continue;
		}
		const auto link = SupportedLink(tag.id);
		const auto suppressLink = !link.isEmpty()
			&& IsSuppressedLinkRun(linkRuns, from, till, link);
		const auto supported = SupportedTags(tag.id, suppressLink);
		if (supported.empty()) {
			continue;
		}
		for (const auto &descriptor : supported) {
			events.push_back({ from, descriptor, 1 });
			events.push_back({ till, descriptor, -1 });
		}
	}
	if (events.empty()) {
		return QString();
	}
	std::sort(events.begin(), events.end(), HtmlTagEventLess);

	auto result = QString();
	result.reserve(text.text.size() * 2);
	auto current = std::vector<HtmlTagDescriptor>();
	auto counts = HtmlTagCounts();
	auto position = 0;
	for (auto i = size_t(0), size = events.size(); i != size;) {
		const auto offset = events[i].offset;
		if (offset > position) {
			auto next = ActiveHtmlTags(counts);
			SwitchTags(result, current, next);
			AppendEscaped(
				result,
				QStringView(text.text).mid(position, offset - position),
				HasTag(next, HtmlTag::Pre));
			current = std::move(next);
			position = offset;
		}
		do {
			UpdateCount(counts, events[i].descriptor, events[i].delta);
			++i;
		} while (i != size && events[i].offset == offset);
	}
	if (position != textSize) {
		auto next = ActiveHtmlTags(counts);
		SwitchTags(result, current, next);
		AppendEscaped(
			result,
			QStringView(text.text).mid(position, textSize - position),
			HasTag(next, HtmlTag::Pre));
		current = std::move(next);
	}
	SwitchTags(result, current, {});
	return u"<html><body>"_q + result + u"</body></html>"_q;
}

QString TextForMimeDataToHtml(const TextForMimeData &text) {
	if (text.rich.text.isEmpty()) {
		return QString();
	}
	const auto tags = ConvertEntitiesToTextTags(HtmlExportEntities(text));
	const auto html = TextWithTagsToHtml({ text.rich.text, tags });
	if (!html.isEmpty()) {
		return html;
	}
	auto result = QString();
	result.reserve(text.rich.text.size() * 2);
	AppendEscaped(result, text.rich.text, false);
	return u"<html><body>"_q + result + u"</body></html>"_q;
}

std::optional<TextWithTags> TextWithTagsFromHtml(QStringView html) {
	auto state = ParseState();
	for (auto i = 0, size = int(html.size()); i != size;) {
		const auto nextTag = html.indexOf(QChar('<'), i);
		if (nextTag < 0) {
			if (state.hidden.empty()) {
				AppendText(state, html.mid(i));
			}
			break;
		}
		if (nextTag > i && state.hidden.empty()) {
			AppendText(state, html.mid(i, nextTag - i));
		}
		i = nextTag;
		if (i + 4 <= size
			&& HasSequence(html, i, u"<!--"_q)) {
			const auto end = FindSequence(html, i + 4, u"-->"_q);
			i = (end < 0) ? size : end + 3;
			continue;
		} else if (i + 2 <= size
			&& (html[i + 1] == '!'
				|| html[i + 1] == '?')) {
			const auto end = FindTagEnd(html, i + 2);
			i = (end < 0) ? size : end + 1;
			continue;
		}
		auto tagStart = i + 1;
		auto closing = false;
		if (tagStart != size && html[tagStart] == '/') {
			closing = true;
			++tagStart;
		}
		const auto tagEnd = FindTagEnd(html, tagStart);
		if (tagEnd < 0) {
			if (state.hidden.empty()) {
				AppendText(state, html.mid(i));
			}
			break;
		}
		auto nameEnd = tagStart;
		const auto name = ReadTagName(html, tagStart, tagEnd, &nameEnd);
		if (name.isEmpty()) {
			if (state.hidden.empty()) {
				AppendText(state, html.mid(i, 1));
			}
			++i;
			continue;
		}
		auto attributes = std::vector<HtmlAttribute>();
		if (!closing) {
			attributes = ReadAttributes(
				html,
				nameEnd,
				tagEnd,
				state.entityCache);
		}
		ProcessTag(
			state,
			name,
			attributes,
			closing,
			IsSelfClosing(html, tagStart, tagEnd));
		i = tagEnd + 1;
	}
	state.result.tags = SimplifyParserTags(std::move(state.tags));
	TrimTrailingStructuralNewlines(state);
	if (state.result.tags.isEmpty() && !state.removedRedundantLinks) {
		return std::nullopt;
	}
	return state.result;
}

} // namespace TextUtilities
