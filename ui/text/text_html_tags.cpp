// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_html_tags.h"

#include "ui/widgets/fields/input_field.h"
#include "ui/basic_click_handlers.h"
#include "base/qthelp_url.h"

#include <QtCore/QHash>
#include <QtGui/QTextDocumentFragment>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace TextUtilities {
namespace {

constexpr auto kMinimalBoldFontWeight = 600;
constexpr auto kCellSourceLengthFactor = 16;

enum class HtmlTag {
	Pre,
	Code,
	Blockquote,
	Link,
	Bold,
	Italic,
	Underline,
	StrikeOut,
	Spoiler,
	Subscript,
	Superscript,
	Marked,
	IvMath,
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
	int spoiler = 0;
	int subscript = 0;
	int superscript = 0;
	int marked = 0;
	int ivMath = 0;
	std::vector<QString> links;
};

struct OpenAnchor {
	QString href;
	int visibleOffset = -1;
};

struct StyleDelta {
	bool bold = false;
	bool italic = false;
	bool underline = false;
	bool strikeOut = false;
	bool spoiler = false;
	bool subscript = false;
	bool superscript = false;
	bool marked = false;
	bool notBold = false;
	bool notItalic = false;
	bool notUnderline = false;
	bool notStrikeOut = false;

	[[nodiscard]] bool empty() const {
		return !bold
			&& !italic
			&& !underline
			&& !strikeOut
			&& !spoiler
			&& !subscript
			&& !superscript
			&& !marked
			&& !notBold
			&& !notItalic
			&& !notUnderline
			&& !notStrikeOut;
	}
};

struct StyleRule {
	StyleDelta delta;
	HtmlTableAlignment alignment = HtmlTableAlignment::Default;

	[[nodiscard]] bool empty() const {
		return delta.empty()
			&& (alignment == HtmlTableAlignment::Default);
	}
};

using StyleClasses = QHash<QString, StyleRule>;

struct OpenElement {
	QString name;
	StyleDelta delta;
};

struct OpenElements {
	std::vector<OpenElement> list;
	QHash<QString, int> counts;

	[[nodiscard]] bool empty() const {
		return list.empty();
	}
	[[nodiscard]] int lastIndexOf(const QString &name) const {
		if (counts.value(name) <= 0) {
			return -1;
		}
		for (auto i = int(list.size()); i != 0; --i) {
			if (list[i - 1].name == name) {
				return i - 1;
			}
		}
		return -1;
	}
	void push(const QString &name, const StyleDelta &delta = StyleDelta()) {
		++counts[name];
		list.push_back({ name, delta });
	}
	void eraseFrom(int index) {
		for (auto i = int(list.size()); i != index; --i) {
			const auto j = counts.find(list[i - 1].name);
			if ((j != counts.end()) && (--j.value() <= 0)) {
				counts.erase(j);
			}
		}
		list.erase(list.begin() + index, list.end());
	}
};

struct ParseState {
	TextWithTags result;
	TextWithTags::Tags tags;
	ActiveTags active;
	std::vector<OpenAnchor> openAnchors;
	OpenElements styledElements;
	const StyleClasses *classes = nullptr;
	NamedEntityCache entityCache;
	OpenElements hidden;
	int trailingStructuralNewlines = 0;
	bool pendingWhitespace = false;
	QString pendingWhitespaceTagId;
	bool removedRedundantLinks = false;
	bool richFormatting = false;
};

struct HtmlTagCounts {
	int pre = 0;
	int code = 0;
	int blockquote = 0;
	int bold = 0;
	int italic = 0;
	int underline = 0;
	int strikeOut = 0;
	int spoiler = 0;
	int subscript = 0;
	int superscript = 0;
	int marked = 0;
	int ivMath = 0;
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
	case HtmlTag::Spoiler: return 6;
	case HtmlTag::Subscript: return 7;
	case HtmlTag::Superscript: return 8;
	case HtmlTag::Marked: return 9;
	case HtmlTag::IvMath: return 12;
	case HtmlTag::Pre: return 10;
	case HtmlTag::Code: return 11;
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
	const auto link = tag.toString();
	const auto external = UrlClickHandler::ExternalUrlFromInternalUrl(link);
	const auto candidate = external.isEmpty() ? link : external;
	return Ui::InputField::IsValidMarkdownLink(candidate)
		&& !TextUtilities::IsMentionLink(candidate);
}

[[nodiscard]] QString SupportedLink(QStringView id) {
	auto result = QString();
	for (const auto &tag : TextUtilities::SplitTags(id)) {
		if (IsSupportedLinkTag(tag)) {
			const auto link = tag.toString();
			const auto external = UrlClickHandler::ExternalUrlFromInternalUrl(
				link);
			result = external.isEmpty() ? link : external;
		}
	}
	return result;
}

[[nodiscard]] QString VisibleEntityText(
		const TextWithEntities &text,
		const EntityInText &entity) {
	const auto textSize = int(text.text.size());
	const auto from = std::clamp(entity.offset(), 0, textSize);
	const auto till
		= std::clamp(entity.offset() + entity.length(), 0, textSize);
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
		case EntityType::CustomUrl: {
			const auto external = UrlClickHandler::ExternalUrlFromInternalUrl(
				entity.data());
			if (!external.isEmpty()) {
				entity = EntityInText(
					EntityType::CustomUrl,
					offset,
					length,
					external);
			}
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
		} else if (tag == Ui::InputField::kTagSpoiler) {
			AddUnique(result, { HtmlTag::Spoiler });
		} else if (tag == Ui::InputField::kTagIvSubscript) {
			AddUnique(result, { HtmlTag::Subscript });
		} else if (tag == Ui::InputField::kTagIvSuperscript) {
			AddUnique(result, { HtmlTag::Superscript });
		} else if (tag == Ui::InputField::kTagIvMarked) {
			AddUnique(result, { HtmlTag::Marked });
		} else if (tag == Ui::InputField::kTagIvMath) {
			AddUnique(result, { HtmlTag::IvMath });
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
	case HtmlTag::Spoiler: counts.spoiler += delta; break;
	case HtmlTag::Subscript: counts.subscript += delta; break;
	case HtmlTag::Superscript: counts.superscript += delta; break;
	case HtmlTag::Marked: counts.marked += delta; break;
	case HtmlTag::IvMath: counts.ivMath += delta; break;
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
	if (counts.spoiler > 0) {
		result.push_back({ HtmlTag::Spoiler });
	}
	if (counts.subscript > 0) {
		result.push_back({ HtmlTag::Subscript });
	}
	if (counts.superscript > 0) {
		result.push_back({ HtmlTag::Superscript });
	}
	if (counts.marked > 0) {
		result.push_back({ HtmlTag::Marked });
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
	case HtmlTag::Spoiler: result.append(u"<tg-spoiler>"_q); break;
	case HtmlTag::Subscript: result.append(u"<sub>"_q); break;
	case HtmlTag::Superscript: result.append(u"<sup>"_q); break;
	case HtmlTag::Marked: result.append(u"<mark>"_q); break;
	case HtmlTag::Link:
		result.append(u"<a href=\""_q);
		result.append(EscapeForHtml(SerializeLinkHref(descriptor.data)));
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
	case HtmlTag::Spoiler: result.append(u"</tg-spoiler>"_q); break;
	case HtmlTag::Subscript: result.append(u"</sub>"_q); break;
	case HtmlTag::Superscript: result.append(u"</sup>"_q); break;
	case HtmlTag::Marked: result.append(u"</mark>"_q); break;
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
	// EscapeForHtml() instead of QString::toHtmlEscaped(): the latter goes
	// through std::u16string_view::find_first_of(), which hangs in an
	// infinite loop on Windows ARM64 builds because of a bug in the MSVC
	// STL Neon implementation (_Impl_first_neon in vector_algorithms.cpp).
	auto start = 0;
	const auto size = text.size();
	for (auto i = 0; i != size; ++i) {
		const auto ch = text[i];
		if (ch != '\r' && ch != '\n') {
			continue;
		}
		result.append(EscapeForHtml(text.mid(start, i - start)));
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
	result.append(EscapeForHtml(text.mid(start)));
}

[[nodiscard]] bool IsTagNameStartChar(QChar ch) {
	return ch.isLetter();
}

[[nodiscard]] bool IsTagNameChar(QChar ch) {
	return ch.isLetterOrNumber() || (ch == '-');
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
	const auto decoded
		= QTextDocumentFragment::fromHtml(source).toPlainText();
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
		attribute.name
			= html.mid(nameStart, from - nameStart).toString().toLower();
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
		const std::vector<HtmlAttribute> &attributes,
		bool allowAnchorLinks) {
	const auto value = AttributeValue(attributes, u"href"_q);
	if (!value) {
		return QString();
	}
	const auto href = value->trimmed();
	if (allowAnchorLinks && href.size() > 1 && href[0] == '#') {
		return href;
	}
	if (!Ui::InputField::IsValidMarkdownLink(href)
		|| TextUtilities::IsMentionLink(href)) {
		return QString();
	}
	const auto protocolMatch = qthelp::RegExpProtocol().match(href);
	if (!protocolMatch.hasMatch()
		|| !qthelp::IsGoodProtocol(protocolMatch.captured(1))) {
		return QString();
	}
	return href;
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
	if (active.spoiler > 0) {
		tags.push_back(Ui::InputField::kTagSpoiler);
	}
	if (active.subscript > 0) {
		tags.push_back(Ui::InputField::kTagIvSubscript);
	}
	if (active.superscript > 0) {
		tags.push_back(Ui::InputField::kTagIvSuperscript);
	}
	if (active.marked > 0) {
		tags.push_back(Ui::InputField::kTagIvMarked);
	}
	if (active.ivMath > 0) {
		tags.push_back(Ui::InputField::kTagIvMath);
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
	const auto visible
		= QStringView(state.result.text).mid(offset, till - offset);
	if (anchor.href
		!= UrlClickHandler::EncodeForOpening(visible.toString())) {
		return;
	}
	auto removed = false;
	for (auto i = state.tags.begin(); i != state.tags.end();) {
		const auto tagTill = i->offset + i->length;
		if (i->offset < offset || tagTill > till) {
			++i;
			continue;
		}
		const auto updated
			= TextUtilities::TagWithRemoved(i->id, anchor.href);
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
	} else if (name == u"tg-spoiler"_q) {
		return HtmlTag::Spoiler;
	} else if (name == u"sub"_q) {
		return HtmlTag::Subscript;
	} else if (name == u"sup"_q) {
		return HtmlTag::Superscript;
	} else if (name == u"mark"_q) {
		return HtmlTag::Marked;
	} else if (name == u"code"_q) {
		return HtmlTag::Code;
	} else if (name == u"pre"_q) {
		return HtmlTag::Pre;
	} else if (name == u"blockquote"_q) {
		return HtmlTag::Blockquote;
	}
	return std::nullopt;
}

[[nodiscard]] QStringView CssValueWithoutImportant(QStringView value) {
	static const auto kImportant = u"!important"_q;
	auto result = value.trimmed();
	if (result.endsWith(kImportant, Qt::CaseInsensitive)) {
		result = result.chopped(kImportant.size()).trimmed();
	}
	return result;
}

[[nodiscard]] QStringView CssDeclarationValue(
		QStringView style,
		QStringView name) {
	auto quote = QChar();
	auto start = 0;
	auto result = QStringView();
	const auto size = int(style.size());
	for (auto i = 0; i <= size; ++i) {
		if (i != size) {
			const auto ch = style[i];
			if (!quote.isNull()) {
				if (ch == quote) {
					quote = QChar();
				}
				continue;
			} else if (ch == '\'' || ch == '"') {
				quote = ch;
				continue;
			} else if (ch != ';') {
				continue;
			}
		}
		const auto declaration = style.mid(start, i - start);
		const auto colon = declaration.indexOf(QChar(':'));
		if (colon > 0) {
			const auto key = declaration.left(colon).trimmed();
			if (!key.compare(name, Qt::CaseInsensitive)) {
				result = CssValueWithoutImportant(declaration.mid(colon + 1));
			}
		}
		start = i + 1;
	}
	return result;
}

[[nodiscard]] std::optional<bool> CssWeightIsBold(QStringView value) {
	if (!value.compare(u"bold"_q, Qt::CaseInsensitive)
		|| !value.compare(u"bolder"_q, Qt::CaseInsensitive)) {
		return true;
	} else if (!value.compare(u"normal"_q, Qt::CaseInsensitive)
		|| !value.compare(u"lighter"_q, Qt::CaseInsensitive)) {
		return false;
	}
	auto ok = false;
	const auto weight = value.toInt(&ok);
	if (!ok) {
		return std::nullopt;
	}
	return (weight >= kMinimalBoldFontWeight);
}

[[nodiscard]] StyleDelta StyleDeltaFromDeclarations(QStringView style) {
	auto result = StyleDelta();
	if (const auto bold = CssWeightIsBold(
			CssDeclarationValue(style, u"font-weight"_q))) {
		result.bold = *bold;
		result.notBold = !*bold;
	}
	const auto fontStyle = CssDeclarationValue(style, u"font-style"_q);
	if (!fontStyle.compare(u"italic"_q, Qt::CaseInsensitive)
		|| !fontStyle.compare(u"oblique"_q, Qt::CaseInsensitive)) {
		result.italic = true;
	} else if (!fontStyle.compare(u"normal"_q, Qt::CaseInsensitive)) {
		result.notItalic = true;
	}
	static const auto kDecorations = std::array{
		u"text-decoration"_q,
		u"text-decoration-line"_q,
	};
	for (const auto &name : kDecorations) {
		const auto value = CssDeclarationValue(style, name);
		if (value.contains(u"underline"_q, Qt::CaseInsensitive)) {
			result.underline = true;
		}
		if (value.contains(u"line-through"_q, Qt::CaseInsensitive)) {
			result.strikeOut = true;
		}
		if (!value.compare(u"none"_q, Qt::CaseInsensitive)) {
			result.notUnderline = true;
			result.notStrikeOut = true;
		}
	}
	const auto verticalAlign = CssDeclarationValue(
		style,
		u"vertical-align"_q);
	if (!verticalAlign.compare(u"sub"_q, Qt::CaseInsensitive)) {
		result.subscript = true;
	} else if (!verticalAlign.compare(u"super"_q, Qt::CaseInsensitive)) {
		result.superscript = true;
	}
	return result;
}

void MergeStyleDelta(StyleDelta &result, const StyleDelta &delta) {
	result.bold = result.bold || delta.bold;
	result.italic = result.italic || delta.italic;
	result.underline = result.underline || delta.underline;
	result.strikeOut = result.strikeOut || delta.strikeOut;
	result.spoiler = result.spoiler || delta.spoiler;
	result.subscript = result.subscript || delta.subscript;
	result.superscript = result.superscript || delta.superscript;
	result.marked = result.marked || delta.marked;
	result.notBold = result.notBold || delta.notBold;
	result.notItalic = result.notItalic || delta.notItalic;
	result.notUnderline = result.notUnderline || delta.notUnderline;
	result.notStrikeOut = result.notStrikeOut || delta.notStrikeOut;
}

[[nodiscard]] HtmlTableAlignment ParseAlignment(QStringView value) {
	if (value.contains(u"right"_q, Qt::CaseInsensitive)) {
		return HtmlTableAlignment::Right;
	} else if (value.contains(u"center"_q, Qt::CaseInsensitive)) {
		return HtmlTableAlignment::Center;
	} else if (value.contains(u"left"_q, Qt::CaseInsensitive)) {
		return HtmlTableAlignment::Left;
	}
	return HtmlTableAlignment::Default;
}

[[nodiscard]] StyleRule StyleRuleFromDeclarations(QStringView style) {
	return {
		.delta = StyleDeltaFromDeclarations(style),
		.alignment = ParseAlignment(
			CssDeclarationValue(style, u"text-align"_q)),
	};
}

[[nodiscard]] StyleRule StyleRuleFromClasses(
		QStringView value,
		const StyleClasses &classes) {
	auto result = StyleRule();
	const auto size = int(value.size());
	for (auto from = 0; from < size;) {
		while (from < size && value[from].isSpace()) {
			++from;
		}
		auto till = from;
		while (till < size && !value[till].isSpace()) {
			++till;
		}
		if (till > from) {
			const auto name = value.mid(from, till - from).toString();
			if (const auto i = classes.find(name); i != classes.end()) {
				MergeStyleDelta(result.delta, i->delta);
				if (i->alignment != HtmlTableAlignment::Default) {
					result.alignment = i->alignment;
				}
			}
		}
		from = till;
	}
	return result;
}

[[nodiscard]] bool ClassListContains(QStringView value, QStringView name) {
	const auto size = int(value.size());
	for (auto from = 0; from < size;) {
		while (from < size && value[from].isSpace()) {
			++from;
		}
		auto till = from;
		while (till < size && !value[till].isSpace()) {
			++till;
		}
		if (till > from && value.mid(from, till - from) == name) {
			return true;
		}
		from = till;
	}
	return false;
}

[[nodiscard]] StyleRule StyleRuleFromAttributes(
		const std::vector<HtmlAttribute> &attributes,
		const StyleClasses *classes) {
	auto fromClasses = StyleRule();
	auto inlined = StyleRule();
	auto align = HtmlTableAlignment::Default;
	auto spoiler = false;
	for (const auto &attribute : attributes) {
		if (!attribute.hasValue) {
			continue;
		} else if (attribute.name == u"style"_q) {
			inlined = StyleRuleFromDeclarations(attribute.value);
		} else if (attribute.name == u"align"_q) {
			align = ParseAlignment(attribute.value);
		} else if (attribute.name == u"class"_q) {
			if (ClassListContains(attribute.value, u"tg-spoiler"_q)) {
				spoiler = true;
			}
			if (classes) {
				fromClasses = StyleRuleFromClasses(
					attribute.value,
					*classes);
			}
		} else if (attribute.name == u"data-entity-type"_q) {
			if (attribute.value == u"MessageEntitySpoiler"_q) {
				spoiler = true;
			}
		}
	}
	auto result = StyleRule();
	MergeStyleDelta(result.delta, fromClasses.delta);
	MergeStyleDelta(result.delta, inlined.delta);
	result.delta.spoiler = result.delta.spoiler || spoiler;
	result.alignment = (inlined.alignment != HtmlTableAlignment::Default)
		? inlined.alignment
		: (fromClasses.alignment != HtmlTableAlignment::Default)
		? fromClasses.alignment
		: align;
	return result;
}

[[nodiscard]] StyleDelta StyleDeltaFromAttributes(
		const std::vector<HtmlAttribute> &attributes,
		const StyleClasses *classes) {
	return StyleRuleFromAttributes(attributes, classes).delta;
}

[[nodiscard]] bool IsStyleInputTag(HtmlTag tag) {
	return (tag == HtmlTag::Bold)
		|| (tag == HtmlTag::Italic)
		|| (tag == HtmlTag::Underline)
		|| (tag == HtmlTag::StrikeOut)
		|| (tag == HtmlTag::Spoiler)
		|| (tag == HtmlTag::Subscript)
		|| (tag == HtmlTag::Superscript)
		|| (tag == HtmlTag::Marked);
}

void ApplyImpliedStyleTag(StyleDelta &delta, HtmlTag tag) {
	switch (tag) {
	case HtmlTag::Bold: delta.bold = true; break;
	case HtmlTag::Italic: delta.italic = true; break;
	case HtmlTag::Underline: delta.underline = true; break;
	case HtmlTag::StrikeOut: delta.strikeOut = true; break;
	case HtmlTag::Spoiler: delta.spoiler = true; break;
	case HtmlTag::Subscript: delta.subscript = true; break;
	case HtmlTag::Superscript: delta.superscript = true; break;
	case HtmlTag::Marked: delta.marked = true; break;
	default: break;
	}
}

void ResolveStyleDelta(StyleDelta &delta) {
	delta.bold = delta.bold && !delta.notBold;
	delta.italic = delta.italic && !delta.notItalic;
	delta.underline = delta.underline && !delta.notUnderline;
	delta.strikeOut = delta.strikeOut && !delta.notStrikeOut;
}

void ApplyStyleDelta(
		ActiveTags &active,
		const StyleDelta &delta,
		bool closing) {
	const auto update = [&](bool set, int &value) {
		if (!set) {
			return;
		} else if (!closing) {
			++value;
		} else if (value > 0) {
			--value;
		}
	};
	update(delta.bold, active.bold);
	update(delta.italic, active.italic);
	update(delta.underline, active.underline);
	update(delta.strikeOut, active.strikeOut);
	update(delta.spoiler, active.spoiler);
	update(delta.subscript, active.subscript);
	update(delta.superscript, active.superscript);
	update(delta.marked, active.marked);
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
	case HtmlTag::Spoiler: update(active.spoiler); break;
	case HtmlTag::Subscript: update(active.subscript); break;
	case HtmlTag::Superscript: update(active.superscript); break;
	case HtmlTag::Marked: update(active.marked); break;
	case HtmlTag::IvMath: update(active.ivMath); break;
	case HtmlTag::Link: break;
	}
}

void CloseHiddenElement(ParseState &state, const QString &name) {
	const auto index = state.hidden.lastIndexOf(name);
	if (index >= 0) {
		state.hidden.eraseFrom(index);
	}
}

[[nodiscard]] bool IsVoidElement(const QString &name) {
	static const auto kElements = std::array{
		u"area"_q,
		u"base"_q,
		u"br"_q,
		u"col"_q,
		u"embed"_q,
		u"hr"_q,
		u"img"_q,
		u"input"_q,
		u"link"_q,
		u"meta"_q,
		u"param"_q,
		u"source"_q,
		u"track"_q,
		u"wbr"_q,
	};
	for (const auto &element : kElements) {
		if (name == element) {
			return true;
		}
	}
	return false;
}

void CloseStyledElement(ParseState &state, const QString &name) {
	const auto index = state.styledElements.lastIndexOf(name);
	if (index < 0) {
		return;
	}
	auto &list = state.styledElements.list;
	for (auto i = int(list.size()); i != index; --i) {
		ApplyStyleDelta(state.active, list[i - 1].delta, true);
	}
	state.styledElements.eraseFrom(index);
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
		} else if (!selfClosing && !IsVoidElement(name)) {
			state.hidden.push(name);
		}
		return;
	}
	if (!closing
		&& !selfClosing
		&& !IsVoidElement(name)
		&& (IsHiddenElement(name) || IsHiddenByAttributes(attributes))) {
		state.hidden.push(name);
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
			const auto href = ValidatedHref(
				attributes,
				state.richFormatting);
			state.active.links.push_back(href);
			state.openAnchors.push_back({ href });
		}
		return;
	}
	const auto blockBoundary = IsBlockBoundary(name);
	if (!closing && blockBoundary) {
		AppendLine(state, false, LineBreakKind::Structural);
	}
	const auto inputTag = SupportedInputTag(name);
	const auto styleInputTag = inputTag && IsStyleInputTag(*inputTag);
	if (inputTag && !styleInputTag && !selfClosing) {
		UpdateActive(state.active, *inputTag, closing);
	}
	if (!IsVoidElement(name)) {
		if (closing) {
			CloseStyledElement(state, name);
		} else if (!selfClosing) {
			auto delta = StyleDeltaFromAttributes(
				attributes,
				state.classes);
			if (styleInputTag) {
				ApplyImpliedStyleTag(delta, *inputTag);
			}
			if (!state.richFormatting) {
				delta.subscript = false;
				delta.superscript = false;
				delta.marked = false;
			}
			ResolveStyleDelta(delta);
			ApplyStyleDelta(state.active, delta, false);
			state.styledElements.push(name, delta);
		}
	}
	if (closing && blockBoundary) {
		AppendLine(state, false, LineBreakKind::Structural);
	}
}

void TruncateTextWithTags(TextWithTags &text, int limit) {
	auto length = std::max(limit, 0);
	if (length >= int(text.text.size())) {
		return;
	} else if ((length > 0) && text.text.at(length - 1).isHighSurrogate()) {
		--length;
	}
	text.text.truncate(length);
	for (auto i = text.tags.begin(); i != text.tags.end();) {
		if (i->offset >= length) {
			i = text.tags.erase(i);
		} else {
			if (i->offset + i->length > length) {
				i->length = length - i->offset;
			}
			++i;
		}
	}
}

void TrimTrailingStructuralNewlines(ParseState &state) {
	if (state.trailingStructuralNewlines <= 0) {
		return;
	}
	TruncateTextWithTags(
		state.result,
		int(state.result.text.size()) - state.trailingStructuralNewlines);
	state.trailingStructuralNewlines = 0;
}

struct ParsedFragment {
	TextWithTags text;
	bool removedRedundantLinks = false;
};

template <typename Text, typename Tag>
void ScanHtml(QStringView html, Text &&text, Tag &&tag) {
	for (auto i = 0, size = int(html.size()); i != size;) {
		const auto nextTag = html.indexOf(QChar('<'), i);
		if (nextTag < 0) {
			text(i, size);
			break;
		}
		if (nextTag > i) {
			text(i, nextTag);
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
			text(i, size);
			break;
		}
		auto nameEnd = tagStart;
		const auto name = ReadTagName(html, tagStart, tagEnd, &nameEnd);
		if (name.isEmpty()) {
			text(i, i + 1);
			++i;
			continue;
		}
		const auto skipTo = tag(
			name,
			i,
			nameEnd,
			tagEnd,
			closing,
			IsSelfClosing(html, tagStart, tagEnd));
		i = (skipTo > tagEnd) ? skipTo : (tagEnd + 1);
	}
}

[[nodiscard]] ParsedFragment ParseFragment(
		QStringView html,
		const StyleDelta &outer = {},
		const StyleClasses *classes = nullptr,
		bool richFormatting = false) {
	auto state = ParseState();
	state.classes = classes;
	state.richFormatting = richFormatting;
	auto resolvedOuter = outer;
	ResolveStyleDelta(resolvedOuter);
	ApplyStyleDelta(state.active, resolvedOuter, false);
	ScanHtml(html, [&](int from, int till) {
		if (state.hidden.empty()) {
			AppendText(state, html.mid(from, till - from));
		}
	}, [&](
			const QString &name,
			int tagFrom,
			int nameEnd,
			int tagEnd,
			bool closing,
			bool selfClosing) {
		auto attributes = std::vector<HtmlAttribute>();
		if (!closing) {
			attributes = ReadAttributes(
				html,
				nameEnd,
				tagEnd,
				state.entityCache);
		}
		ProcessTag(state, name, attributes, closing, selfClosing);
		return -1;
	});
	state.result.tags = SimplifyParserTags(std::move(state.tags));
	TrimTrailingStructuralNewlines(state);
	return {
		.text = std::move(state.result),
		.removedRedundantLinks = state.removedRedundantLinks,
	};
}

struct TableScanCell {
	int contentFrom = 0;
	int contentTill = 0;
	int colspan = 1;
	int rowspan = 1;
	bool header = false;
	HtmlTableAlignment alignment = HtmlTableAlignment::Default;
	StyleDelta style;
};

struct TableScanRow {
	std::vector<TableScanCell> cells;
};

struct TableScanResult {
	std::vector<TableScanRow> rows;
	int captionFrom = -1;
	int captionTill = -1;
	int sourceTill = 0;
	bool truncated = false;
};

[[nodiscard]] const QString *AttributeValue(
		const std::vector<HtmlAttribute> &attributes,
		const QString &name) {
	for (const auto &attribute : attributes) {
		if (attribute.hasValue && attribute.name == name) {
			return &attribute.value;
		}
	}
	return nullptr;
}

[[nodiscard]] int AttributeSpan(
		const std::vector<HtmlAttribute> &attributes,
		const QString &name,
		int limit) {
	const auto value = AttributeValue(attributes, name);
	if (!value) {
		return 1;
	}
	auto ok = false;
	const auto parsed = value->trimmed().toInt(&ok);
	return ok ? std::clamp(parsed, 1, limit) : 1;
}

[[nodiscard]] bool NameIsCellStart(const QString &name) {
	return (name == u"td"_q) || (name == u"th"_q);
}

[[nodiscard]] int FindTagStart(QStringView html, QStringView name, int from) {
	for (auto i = from;;) {
		const auto index = html.indexOf(name, i, Qt::CaseInsensitive);
		if (index < 0) {
			return -1;
		}
		const auto after = index + int(name.size());
		if (after >= int(html.size())) {
			return -1;
		}
		const auto next = html[after];
		if (next.isSpace() || (next == '>') || (next == '/')) {
			return index;
		}
		i = index + 1;
	}
}

[[nodiscard]] QString StyleClassName(QStringView selector) {
	const auto trimmed = selector.trimmed();
	const auto dot = trimmed.lastIndexOf(QChar('.'));
	if (dot < 0) {
		return QString();
	}
	const auto name = trimmed.mid(dot + 1);
	if (name.isEmpty()) {
		return QString();
	}
	for (const auto ch : name) {
		if (!ch.isLetterOrNumber() && (ch != '-') && (ch != '_')) {
			return QString();
		}
	}
	return name.toString();
}

void ParseStyleRules(QStringView css, not_null<StyleClasses*> classes) {
	const auto size = int(css.size());
	for (auto from = 0; from < size;) {
		const auto open = css.indexOf(QChar('{'), from);
		if (open < 0) {
			return;
		}
		const auto close = css.indexOf(QChar('}'), open + 1);
		const auto till = (close < 0) ? size : close;
		const auto rule = StyleRuleFromDeclarations(
			css.mid(open + 1, till - open - 1));
		if (!rule.empty()) {
			const auto selectors = css.mid(from, open - from);
			const auto count = int(selectors.size());
			for (auto start = 0; start <= count;) {
				auto end = selectors.indexOf(QChar(','), start);
				if (end < 0) {
					end = count;
				}
				const auto name = StyleClassName(
					selectors.mid(start, end - start));
				if (!name.isEmpty()) {
					auto &entry = (*classes)[name];
					MergeStyleDelta(entry.delta, rule.delta);
					if (rule.alignment != HtmlTableAlignment::Default) {
						entry.alignment = rule.alignment;
					}
				}
				start = end + 1;
			}
		}
		if (close < 0) {
			return;
		}
		from = close + 1;
	}
}

[[nodiscard]] StyleClasses ParseStyleClasses(QStringView html) {
	auto result = StyleClasses();
	for (auto i = 0;;) {
		const auto open = FindTagStart(html, u"<style"_q, i);
		if (open < 0) {
			return result;
		}
		const auto contentFrom = FindTagEnd(html, open + 6);
		if (contentFrom < 0) {
			return result;
		}
		const auto close = html.indexOf(
			u"</style"_q,
			contentFrom,
			Qt::CaseInsensitive);
		const auto contentTill = (close < 0) ? int(html.size()) : close;
		ParseStyleRules(
			html.mid(contentFrom + 1, contentTill - contentFrom - 1),
			&result);
		if (close < 0) {
			return result;
		}
		i = close + 7;
	}
}

[[nodiscard]] TableScanResult ScanTable(
		QStringView html,
		int from,
		const HtmlTableLimits &limits,
		const StyleClasses &classes) {
	auto result = TableScanResult();
	auto entityCache = NamedEntityCache();
	auto hidden = OpenElements();
	auto depth = 0;
	auto headerSection = 0;
	auto cell = std::optional<TableScanCell>();
	auto row = std::optional<TableScanRow>();
	auto cells = 0;
	auto finished = false;
	auto skippedRow = false;

	const auto finishCell = [&](int till) {
		if (!cell || !row) {
			return;
		}
		cell->contentTill = std::max(till, cell->contentFrom);
		row->cells.push_back(*cell);
		cell = std::nullopt;
	};
	const auto finishRow = [&](int till) {
		finishCell(till);
		if (!row) {
			return;
		}
		if (!row->cells.empty()) {
			result.rows.push_back(std::move(*row));
		}
		row = std::nullopt;
	};

	for (auto i = from, size = int(html.size()); i != size && !finished;) {
		const auto nextTag = html.indexOf(QChar('<'), i);
		if (nextTag < 0) {
			break;
		}
		i = nextTag;
		if (i + 4 <= size && HasSequence(html, i, u"<!--"_q)) {
			const auto end = FindSequence(html, i + 4, u"-->"_q);
			i = (end < 0) ? size : end + 3;
			continue;
		} else if (i + 2 <= size
			&& (html[i + 1] == '!' || html[i + 1] == '?')) {
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
			break;
		}
		auto nameEnd = tagStart;
		const auto name = ReadTagName(html, tagStart, tagEnd, &nameEnd);
		if (name.isEmpty()) {
			++i;
			continue;
		}
		const auto selfClosing = IsSelfClosing(html, tagStart, tagEnd);
		const auto tagFrom = i;
		i = tagEnd + 1;

		if (!hidden.empty()) {
			if (closing) {
				const auto index = hidden.lastIndexOf(name);
				if (index >= 0) {
					hidden.eraseFrom(index);
				}
			} else if (!selfClosing && !IsVoidElement(name)) {
				hidden.push(name);
			}
			continue;
		} else if (!closing
			&& !selfClosing
			&& !IsVoidElement(name)
			&& IsHiddenElement(name)) {
			hidden.push(name);
			continue;
		}

		if (name == u"table"_q) {
			if (closing) {
				if (depth <= 1) {
					finishRow(tagFrom);
					result.sourceTill = i;
					finished = true;
				} else {
					--depth;
				}
			} else if (!selfClosing) {
				++depth;
			}
			continue;
		} else if (!depth || (depth > 1)) {
			continue;
		}

		if (name == u"thead"_q) {
			if (!selfClosing) {
				headerSection += closing ? -1 : 1;
				headerSection = std::max(headerSection, 0);
			}
		} else if (name == u"caption"_q) {
			if (closing) {
				if (result.captionFrom >= 0 && result.captionTill < 0) {
					result.captionTill = tagFrom;
				}
			} else if (!selfClosing && result.captionFrom < 0) {
				result.captionFrom = i;
			}
		} else if (!closing
			&& ((name == u"tbody"_q) || (name == u"tfoot"_q))) {
			headerSection = 0;
		} else if (name == u"tr"_q) {
			finishRow(tagFrom);
			skippedRow = false;
			if (!closing && !selfClosing) {
				if (int(result.rows.size()) >= limits.maxRows) {
					result.truncated = true;
					finished = true;
					continue;
				}
				const auto attributes = ReadAttributes(
					html,
					nameEnd,
					tagEnd,
					entityCache);
				if (IsHiddenByAttributes(attributes)) {
					skippedRow = true;
					continue;
				}
				row = TableScanRow();
			}
		} else if (NameIsCellStart(name)) {
			if (skippedRow) {
				continue;
			}
			finishCell(tagFrom);
			if (closing || selfClosing) {
				continue;
			} else if (cells >= limits.maxCells) {
				result.truncated = true;
				finished = true;
				continue;
			} else if (!row) {
				row = TableScanRow();
			}
			++cells;
			const auto attributes = ReadAttributes(
				html,
				nameEnd,
				tagEnd,
				entityCache);
			const auto rule = StyleRuleFromAttributes(attributes, &classes);
			cell = TableScanCell{
				.contentFrom = i,
				.contentTill = i,
				.colspan = AttributeSpan(
					attributes,
					u"colspan"_q,
					limits.maxColumns),
				.rowspan = AttributeSpan(
					attributes,
					u"rowspan"_q,
					limits.maxRows),
				.header = (name == u"th"_q) || (headerSection > 0),
				.alignment = rule.alignment,
				.style = rule.delta,
			};
			if (IsHiddenByAttributes(attributes)) {
				finishCell(i);
			}
		}
	}
	finishRow(int(html.size()));
	if (!result.sourceTill) {
		result.sourceTill = int(html.size());
	}
	return result;
}

void NormalizeTable(HtmlTable &table, const HtmlTableLimits &limits) {
	auto spans = std::vector<int>();
	auto used = std::vector<int>();
	used.reserve(table.rows.size());
	for (auto &row : table.rows) {
		auto column = 0;
		auto kept = 0;
		for (auto &cell : row.cells) {
			while ((column < int(spans.size())) && (spans[column] > 0)) {
				++column;
			}
			if (column + cell.colspan > limits.maxColumns) {
				table.truncated = true;
				break;
			}
			if (int(spans.size()) < column + cell.colspan) {
				spans.resize(column + cell.colspan, 0);
			}
			for (auto j = 0; j != cell.colspan; ++j) {
				spans[column + j] = cell.rowspan;
			}
			column += cell.colspan;
			++kept;
		}
		row.cells.resize(kept);
		auto accounted = column;
		auto extent = column;
		for (auto j = column; j != int(spans.size()); ++j) {
			if (spans[j] > 0) {
				++accounted;
				extent = j + 1;
			}
		}
		used.push_back(accounted);
		table.columns = std::max(table.columns, extent);
		for (auto &value : spans) {
			if (value > 0) {
				--value;
			}
		}
	}
	for (auto i = 0, count = int(table.rows.size()); i != count; ++i) {
		for (auto j = used[i]; j < table.columns; ++j) {
			table.rows[i].cells.push_back(HtmlTableCell());
		}
	}
}

[[nodiscard]] std::optional<HtmlTable> TableFromScan(
		QStringView html,
		const TableScanResult &scanned,
		const HtmlTableLimits &limits,
		const StyleClasses &classes) {
	if (scanned.rows.empty()) {
		return std::nullopt;
	}
	auto result = HtmlTable();
	result.sourceTill = scanned.sourceTill;
	result.truncated = scanned.truncated;
	result.rows.reserve(scanned.rows.size());
	const auto sourceLimit = int(std::min(
		int64(std::max(limits.maxCellLength, 0)) * kCellSourceLengthFactor,
		int64(html.size())));
	if (scanned.captionFrom >= 0 && scanned.captionTill > scanned.captionFrom) {
		auto length = scanned.captionTill - scanned.captionFrom;
		if (length > sourceLimit) {
			length = sourceLimit;
			result.truncated = true;
		}
		auto caption = ParseFragment(
			html.mid(scanned.captionFrom, length),
			StyleDelta(),
			&classes,
			true).text;
		if (int(caption.text.size()) > limits.maxCellLength) {
			TruncateTextWithTags(caption, limits.maxCellLength);
			result.truncated = true;
		}
		result.caption = std::move(caption);
	}
	for (const auto &scannedRow : scanned.rows) {
		auto row = HtmlTableRow();
		row.cells.reserve(scannedRow.cells.size());
		for (const auto &scannedCell : scannedRow.cells) {
			auto length = scannedCell.contentTill - scannedCell.contentFrom;
			if (length > sourceLimit) {
				length = sourceLimit;
				result.truncated = true;
			}
			auto text = ParseFragment(
				html.mid(scannedCell.contentFrom, length),
				scannedCell.style,
				&classes,
				true).text;
			if (text.text.size() > limits.maxCellLength) {
				TruncateTextWithTags(text, limits.maxCellLength);
				result.truncated = true;
			}
			row.cells.push_back({
				.text = std::move(text),
				.colspan = scannedCell.colspan,
				.rowspan = scannedCell.rowspan,
				.header = scannedCell.header,
				.alignment = scannedCell.alignment,
			});
		}
		result.rows.push_back(std::move(row));
	}
	NormalizeTable(result, limits);
	if (!result.columns) {
		return std::nullopt;
	}
	return result;
}

struct BlockContainer {
	QString element;
	HtmlBlock block;
	HtmlTaskState taskState = HtmlTaskState::None;
	std::optional<int> itemValue;
	bool isListItem = false;
	bool styled = false;
	bool unwrap = false;
};

struct BlockParseState {
	ParseState content;
	std::vector<HtmlBlock> blocks;
	std::vector<BlockContainer> stack;
	const HtmlBlocksLimits *limits = nullptr;
	HtmlBlockKind leafKind = HtmlBlockKind::Paragraph;
	int headingLevel = 0;
	QString codeLanguage;
	QString leafAnchorId;
	int preDepth = 0;
	int blockCount = 0;
	int totalLength = 0;
	bool inSummary = false;
	bool inCaption = false;
	bool truncated = false;
};

[[nodiscard]] QString AnchorIdFromAttributes(
		const std::vector<HtmlAttribute> &attributes) {
	constexpr auto kMaxAnchorIdLength = 64;
	const auto value = AttributeValue(attributes, u"id"_q);
	if (!value) {
		return QString();
	}
	const auto trimmed = value->trimmed();
	return (trimmed.size() > kMaxAnchorIdLength) ? QString() : trimmed;
}

[[nodiscard]] std::vector<HtmlBlock> &CurrentBlocks(BlockParseState &state) {
	return state.stack.empty()
		? state.blocks
		: state.stack.back().block.children;
}

[[nodiscard]] int RealContainerDepth(const BlockParseState &state) {
	auto result = 0;
	for (const auto &container : state.stack) {
		if (!container.unwrap) {
			++result;
		}
	}
	return result;
}

[[nodiscard]] bool HasOpenContainer(
		const BlockParseState &state,
		const QString &element) {
	for (const auto &container : state.stack) {
		if (container.element == element) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool IsListContainer(const BlockContainer &container) {
	return (container.block.kind == HtmlBlockKind::List)
		&& !container.isListItem;
}

void EnsureListItemTarget(BlockParseState &state) {
	if (state.stack.empty()) {
		return;
	}
	const auto &top = state.stack.back();
	if (!IsListContainer(top) || top.unwrap) {
		return;
	}
	auto container = BlockContainer();
	container.element = u"li"_q;
	container.isListItem = true;
	state.stack.push_back(std::move(container));
}

[[nodiscard]] int HeadingLevelFromName(const QString &name) {
	if (name.size() == 2
		&& name[0] == 'h'
		&& name[1] >= '1'
		&& name[1] <= '6') {
		return name[1].unicode() - '0';
	}
	return 0;
}

[[nodiscard]] bool IsParagraphFlushBoundary(const QString &name) {
	if (name == u"figure"_q) {
		return true;
	}
	return IsBlockBoundary(name)
		&& (name != u"div"_q)
		&& (name != u"blockquote"_q)
		&& (name != u"pre"_q);
}

[[nodiscard]] TextWithTags TakeBlockText(ParseState &state) {
	state.result.tags = SimplifyParserTags(std::move(state.tags));
	state.tags = TextWithTags::Tags();
	TrimTrailingStructuralNewlines(state);
	auto result = std::move(state.result);
	state.result = TextWithTags();
	ClearPendingWhitespace(state);
	state.trailingStructuralNewlines = 0;
	for (auto &anchor : state.openAnchors) {
		anchor.visibleOffset = -1;
	}
	return result;
}

void TrimTrailingBlockWhitespace(TextWithTags &text) {
	auto length = int(text.text.size());
	while (length > 0 && text.text[length - 1].isSpace()) {
		--length;
	}
	if (length < int(text.text.size())) {
		TruncateTextWithTags(text, length);
	}
}

void StripCodeBlockText(TextWithTags &text) {
	text.tags.clear();
	if (!text.text.isEmpty() && text.text.front() == '\n') {
		text.text.remove(0, 1);
	}
}

[[nodiscard]] bool EmitBlockAllowed(BlockParseState &state) {
	if (state.blockCount >= state.limits->maxBlocks) {
		state.truncated = true;
		return false;
	}
	return true;
}

void AppendLeafBlock(BlockParseState &state, TextWithTags text) {
	if (!EmitBlockAllowed(state)) {
		return;
	}
	const auto &limits = *state.limits;
	const auto remaining = limits.maxTotalLength - state.totalLength;
	if (remaining <= 0) {
		state.truncated = true;
		return;
	}
	const auto cap = std::min(limits.maxBlockLength, remaining);
	if (int(text.text.size()) > cap) {
		TruncateTextWithTags(text, cap);
		state.truncated = true;
		if (text.text.isEmpty()) {
			return;
		}
	}
	state.totalLength += int(text.text.size());
	auto block = HtmlBlock();
	block.kind = state.leafKind;
	block.text = std::move(text);
	block.anchorId = std::exchange(state.leafAnchorId, QString());
	if (state.leafKind == HtmlBlockKind::Heading) {
		block.headingLevel = state.headingLevel;
	} else if (state.leafKind == HtmlBlockKind::Code) {
		block.language = state.codeLanguage;
	}
	++state.blockCount;
	CurrentBlocks(state).push_back(std::move(block));
}

[[nodiscard]] bool IsMediaBlockKind(HtmlBlockKind kind) {
	return (kind == HtmlBlockKind::Photo)
		|| (kind == HtmlBlockKind::Video)
		|| (kind == HtmlBlockKind::Audio);
}

[[nodiscard]] bool BlockAcceptsCaption(HtmlBlockKind kind) {
	switch (kind) {
	case HtmlBlockKind::Quote:
	case HtmlBlockKind::Pullquote:
	case HtmlBlockKind::Photo:
	case HtmlBlockKind::Video:
	case HtmlBlockKind::Audio:
	case HtmlBlockKind::Collage:
	case HtmlBlockKind::Slideshow:
	case HtmlBlockKind::Map:
		return true;
	default:
		return false;
	}
}

void FlushLeafBlock(BlockParseState &state) {
	auto text = TakeBlockText(state.content);
	if (state.leafKind == HtmlBlockKind::Code) {
		StripCodeBlockText(text);
	}
	TrimTrailingBlockWhitespace(text);
	if (text.text.isEmpty()) {
		state.leafAnchorId = QString();
		return;
	}
	if (state.inSummary) {
		for (auto i = int(state.stack.size()); i != 0; --i) {
			auto &container = state.stack[i - 1];
			if (container.block.kind == HtmlBlockKind::Details
				&& !container.isListItem) {
				if (container.block.text.text.isEmpty()) {
					container.block.text = std::move(text);
				}
				state.leafAnchorId = QString();
				return;
			}
		}
	}
	if (state.inCaption) {
		for (auto i = int(state.stack.size()); i != 0; --i) {
			auto &container = state.stack[i - 1];
			if (container.isListItem) {
				break;
			} else if (container.block.kind == HtmlBlockKind::Collage
				|| container.block.kind == HtmlBlockKind::Slideshow) {
				if (container.block.caption.text.isEmpty()) {
					container.block.caption = std::move(text);
				}
				state.leafAnchorId = QString();
				return;
			}
		}
		auto &blocks = CurrentBlocks(state);
		if (!blocks.empty()
			&& BlockAcceptsCaption(blocks.back().kind)
			&& blocks.back().caption.text.isEmpty()) {
			blocks.back().caption = std::move(text);
			state.leafAnchorId = QString();
			return;
		}
	}
	EnsureListItemTarget(state);
	AppendLeafBlock(state, std::move(text));
}

void AppendDividerBlock(BlockParseState &state) {
	if (!EmitBlockAllowed(state)) {
		return;
	}
	EnsureListItemTarget(state);
	auto block = HtmlBlock();
	block.kind = HtmlBlockKind::Divider;
	++state.blockCount;
	CurrentBlocks(state).push_back(std::move(block));
}

void PushStyledBlockElement(
		BlockParseState &state,
		const QString &name,
		const std::vector<HtmlAttribute> &attributes) {
	auto delta = StyleDeltaFromAttributes(attributes, state.content.classes);
	ResolveStyleDelta(delta);
	ApplyStyleDelta(state.content.active, delta, false);
	state.content.styledElements.push(name, delta);
}

[[nodiscard]] bool AllChildrenAreParagraphs(const HtmlBlock &block) {
	for (const auto &child : block.children) {
		if (child.kind != HtmlBlockKind::Paragraph) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] TextWithTags JoinParagraphTexts(std::vector<HtmlBlock> &blocks) {
	auto result = TextWithTags();
	for (auto &block : blocks) {
		if (!result.text.isEmpty()) {
			result.text.append(QChar('\n'));
		}
		const auto offset = int(result.text.size());
		for (auto tag : block.text.tags) {
			tag.offset += offset;
			result.tags.push_back(std::move(tag));
		}
		result.text.append(block.text.text);
	}
	return result;
}

void OpenBlockContainer(
		BlockParseState &state,
		HtmlBlockKind kind,
		const QString &name,
		const std::vector<HtmlAttribute> &attributes) {
	FlushLeafBlock(state);
	EnsureListItemTarget(state);
	auto container = BlockContainer();
	container.element = name;
	container.block.kind = kind;
	container.block.anchorId = AnchorIdFromAttributes(attributes);
	container.styled = true;
	if (RealContainerDepth(state) >= state.limits->maxDepth) {
		container.unwrap = true;
		state.truncated = true;
	}
	state.stack.push_back(std::move(container));
	PushStyledBlockElement(state, name, attributes);
}

[[nodiscard]] std::optional<int> AttributeInt(
		const std::vector<HtmlAttribute> &attributes,
		const QString &name) {
	const auto value = AttributeValue(attributes, name);
	if (!value) {
		return std::nullopt;
	}
	auto ok = false;
	const auto parsed = value->trimmed().toInt(&ok);
	if (!ok) {
		return std::nullopt;
	}
	return parsed;
}

void OpenListContainer(
		BlockParseState &state,
		const QString &name,
		const std::vector<HtmlAttribute> &attributes) {
	OpenBlockContainer(state, HtmlBlockKind::List, name, attributes);
	auto &block = state.stack.back().block;
	if (name != u"ol"_q) {
		return;
	}
	block.listKind = HtmlListKind::Ordered;
	block.listStart = AttributeInt(attributes, u"start"_q);
	block.listReversed = HasAttribute(attributes, u"reversed"_q);
	if (const auto type = AttributeValue(attributes, u"type"_q)) {
		const auto trimmed = type->trimmed();
		if (trimmed.size() == 1
			&& QStringView(u"1aAiI").contains(trimmed[0])) {
			block.listType = trimmed;
		}
	}
}

[[nodiscard]] int NearestListIndex(const BlockParseState &state) {
	for (auto i = int(state.stack.size()); i != 0; --i) {
		if (IsListContainer(state.stack[i - 1])) {
			return i - 1;
		}
	}
	return -1;
}

[[nodiscard]] int NearestListItemIndex(const BlockParseState &state) {
	for (auto i = int(state.stack.size()); i != 0; --i) {
		if (state.stack[i - 1].isListItem) {
			return i - 1;
		}
	}
	return -1;
}

void FinishListItemContainer(
		BlockParseState &state,
		BlockContainer container) {
	auto item = HtmlListItem();
	item.taskState = container.taskState;
	item.value = container.itemValue;
	item.anchorId = std::move(container.block.anchorId);
	auto &children = container.block.children;
	if (children.size() == 1
		&& children.front().kind == HtmlBlockKind::Paragraph
		&& children.front().children.empty()) {
		item.text = std::move(children.front().text);
		--state.blockCount;
	} else {
		item.blocks = std::move(children);
	}
	const auto empty = item.text.text.isEmpty()
		&& item.blocks.empty()
		&& (item.taskState == HtmlTaskState::None);
	if (empty) {
		return;
	}
	if (!state.stack.empty()
		&& IsListContainer(state.stack.back())
		&& !state.stack.back().unwrap) {
		if (!EmitBlockAllowed(state)) {
			return;
		}
		++state.blockCount;
		state.stack.back().block.items.push_back(std::move(item));
		return;
	}
	auto &parent = CurrentBlocks(state);
	if (!item.text.text.isEmpty()) {
		if (!EmitBlockAllowed(state)) {
			return;
		}
		auto paragraph = HtmlBlock();
		paragraph.text = std::move(item.text);
		++state.blockCount;
		parent.push_back(std::move(paragraph));
	}
	for (auto &child : item.blocks) {
		parent.push_back(std::move(child));
	}
}

void FinishBlockContainer(BlockParseState &state, BlockContainer container) {
	if (container.isListItem) {
		FinishListItemContainer(state, std::move(container));
		return;
	}
	if (container.unwrap) {
		auto &parent = CurrentBlocks(state);
		for (auto &child : container.block.children) {
			parent.push_back(std::move(child));
		}
		return;
	}
	auto &block = container.block;
	if (block.kind == HtmlBlockKind::Quote
		|| block.kind == HtmlBlockKind::Pullquote) {
		if (block.children.empty()) {
			return;
		} else if (block.children.size() == 1
			&& block.children.front().kind == HtmlBlockKind::Paragraph) {
			block.text = std::move(block.children.front().text);
			block.children.clear();
			--state.blockCount;
		}
	} else if (block.kind == HtmlBlockKind::Footer) {
		if (block.children.empty()) {
			return;
		} else if (AllChildrenAreParagraphs(block)) {
			block.text = JoinParagraphTexts(block.children);
			state.blockCount -= int(block.children.size());
			block.children.clear();
		} else {
			auto &parent = CurrentBlocks(state);
			for (auto &child : block.children) {
				parent.push_back(std::move(child));
			}
			return;
		}
	} else if (block.kind == HtmlBlockKind::List) {
		if (block.items.empty()) {
			auto &parent = CurrentBlocks(state);
			for (auto &child : block.children) {
				parent.push_back(std::move(child));
			}
			return;
		}
	} else if (block.kind == HtmlBlockKind::Details) {
		if (block.text.text.isEmpty() && block.children.empty()) {
			return;
		}
	} else if (block.kind == HtmlBlockKind::Collage
		|| block.kind == HtmlBlockKind::Slideshow) {
		auto media = 0;
		for (const auto &child : block.children) {
			if (IsMediaBlockKind(child.kind)) {
				++media;
			}
		}
		if (!media) {
			auto &parent = CurrentBlocks(state);
			for (auto &child : block.children) {
				parent.push_back(std::move(child));
			}
			return;
		}
	}
	if (!EmitBlockAllowed(state)) {
		return;
	}
	EnsureListItemTarget(state);
	++state.blockCount;
	CurrentBlocks(state).push_back(std::move(block));
}

void PopBlockContainer(BlockParseState &state) {
	FlushLeafBlock(state);
	auto container = std::move(state.stack.back());
	state.stack.pop_back();
	FinishBlockContainer(state, std::move(container));
}

void CloseBlockContainer(BlockParseState &state, const QString &name) {
	auto index = -1;
	for (auto i = int(state.stack.size()); i != 0; --i) {
		if (state.stack[i - 1].element == name) {
			index = i - 1;
			break;
		}
	}
	if (index < 0) {
		return;
	}
	while (int(state.stack.size()) > index) {
		PopBlockContainer(state);
	}
}

void CloseListItemAt(BlockParseState &state, int itemIndex) {
	if (state.stack[itemIndex].styled) {
		CloseStyledElement(state.content, u"li"_q);
	}
	while (int(state.stack.size()) > itemIndex) {
		PopBlockContainer(state);
	}
}

void ProcessListItemTag(
		BlockParseState &state,
		const std::vector<HtmlAttribute> &attributes,
		bool closing,
		bool selfClosing) {
	if (closing) {
		const auto listIndex = NearestListIndex(state);
		const auto itemIndex = NearestListItemIndex(state);
		if (itemIndex >= 0 && itemIndex > listIndex) {
			CloseListItemAt(state, itemIndex);
		}
		return;
	}
	FlushLeafBlock(state);
	const auto listIndex = NearestListIndex(state);
	const auto itemIndex = NearestListItemIndex(state);
	if (itemIndex >= 0 && itemIndex > listIndex) {
		CloseListItemAt(state, itemIndex);
	}
	if (state.stack.empty()
		|| !IsListContainer(state.stack.back())
		|| state.stack.back().unwrap) {
		return;
	}
	if (selfClosing) {
		return;
	}
	auto container = BlockContainer();
	container.element = u"li"_q;
	container.isListItem = true;
	container.itemValue = AttributeInt(attributes, u"value"_q);
	container.block.anchorId = AnchorIdFromAttributes(attributes);
	container.styled = true;
	state.stack.push_back(std::move(container));
	PushStyledBlockElement(state, u"li"_q, attributes);
}

[[nodiscard]] std::optional<HtmlBlockKind> MediaKindFromName(
		const QString &name) {
	if (name == u"img"_q) {
		return HtmlBlockKind::Photo;
	} else if (name == u"video"_q) {
		return HtmlBlockKind::Video;
	} else if (name == u"audio"_q) {
		return HtmlBlockKind::Audio;
	}
	return std::nullopt;
}

[[nodiscard]] bool IsMediaGroupName(const QString &name) {
	return (name == u"tg-collage"_q) || (name == u"tg-slideshow"_q);
}

[[nodiscard]] bool HasClass(
		const std::vector<HtmlAttribute> &attributes,
		const QString &name) {
	const auto value = AttributeValue(attributes, u"class"_q);
	if (!value) {
		return false;
	}
	const auto list = value->split(QChar(' '), Qt::SkipEmptyParts);
	for (const auto &entry : list) {
		if (entry == name) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool HasOpenMediaGroup(const BlockParseState &state) {
	for (const auto &container : state.stack) {
		if ((container.block.kind == HtmlBlockKind::Collage)
			|| (container.block.kind == HtmlBlockKind::Slideshow)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool IsOwnMediaTag(
		const std::vector<HtmlAttribute> &attributes) {
	return HasAttribute(attributes, u"data-tg-src"_q)
		|| HasAttribute(attributes, u"data-tg-math"_q)
		|| HasClass(attributes, u"math-image"_q)
		|| HasClass(attributes, u"math-inline"_q)
		|| HasClass(attributes, u"inline-image"_q);
}

[[nodiscard]] bool HasOpenMediaGroupContainer(
		const BlockParseState &state,
		const QString &element) {
	for (const auto &container : state.stack) {
		if ((container.element == element)
			&& ((container.block.kind == HtmlBlockKind::Collage)
				|| (container.block.kind == HtmlBlockKind::Slideshow))) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool HasInlineMathTag(const TextWithTags::Tags &tags) {
	for (const auto &tag : tags) {
		if (TextUtilities::SplitTags(tag.id).contains(
				QStringView(Ui::InputField::kTagIvMath))) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool IsInlineMathImage(
		const std::vector<HtmlAttribute> &attributes) {
	return HasClass(attributes, u"math-inline"_q);
}

[[nodiscard]] std::optional<QString> ImageMathFormula(
		const std::vector<HtmlAttribute> &attributes) {
	const auto alt = [&] {
		const auto value = AttributeValue(attributes, u"alt"_q);
		return value ? *value : QString();
	};
	if (HasAttribute(attributes, u"data-tg-math"_q)) {
		const auto value = AttributeValue(attributes, u"data-tg-math"_q);
		return (value && !value->isEmpty()) ? *value : alt();
	}
	return (HasClass(attributes, u"math-image"_q)
		|| HasClass(attributes, u"math-inline"_q))
		? std::make_optional(alt())
		: std::nullopt;
}

void AppendMathBlock(
		BlockParseState &state,
		const QString &formula,
		const std::vector<HtmlAttribute> &attributes) {
	FlushLeafBlock(state);
	if (formula.isEmpty() || !EmitBlockAllowed(state)) {
		return;
	}
	EnsureListItemTarget(state);
	auto block = HtmlBlock();
	block.kind = HtmlBlockKind::Math;
	block.anchorId = AnchorIdFromAttributes(attributes);
	block.formula = formula;
	++state.blockCount;
	CurrentBlocks(state).push_back(std::move(block));
}

void AppendMediaBlock(
		BlockParseState &state,
		HtmlBlockKind kind,
		const std::vector<HtmlAttribute> &attributes) {
	FlushLeafBlock(state);
	const auto source = AttributeValue(attributes, u"src"_q);
	if (!source || source->trimmed().isEmpty()) {
		return;
	} else if (!EmitBlockAllowed(state)) {
		return;
	}
	EnsureListItemTarget(state);
	auto block = HtmlBlock();
	block.kind = kind;
	block.anchorId = AnchorIdFromAttributes(attributes);
	block.media.source = source->trimmed();
	if (const auto identity = AttributeValue(attributes, u"data-tg-src"_q)) {
		block.media.identity = identity->trimmed();
	}
	block.media.width = AttributeInt(attributes, u"width"_q).value_or(0);
	block.media.height = AttributeInt(attributes, u"height"_q).value_or(0);
	block.media.spoiler = HasAttribute(attributes, u"tg-spoiler"_q);
	block.media.autoplay = HasAttribute(attributes, u"autoplay"_q);
	block.media.loop = HasAttribute(attributes, u"loop"_q);
	++state.blockCount;
	CurrentBlocks(state).push_back(std::move(block));
}

void AppendMapBlock(
		BlockParseState &state,
		const std::vector<HtmlAttribute> &attributes) {
	FlushLeafBlock(state);
	const auto latitude = AttributeValue(attributes, u"lat"_q);
	const auto longitude = AttributeValue(attributes, u"long"_q);
	if (!latitude || !longitude) {
		return;
	} else if (!EmitBlockAllowed(state)) {
		return;
	}
	EnsureListItemTarget(state);
	auto block = HtmlBlock();
	block.kind = HtmlBlockKind::Map;
	block.anchorId = AnchorIdFromAttributes(attributes);
	block.mapPoint.latitude = latitude->trimmed();
	block.mapPoint.longitude = longitude->trimmed();
	block.mapPoint.zoom = AttributeInt(attributes, u"zoom"_q).value_or(0);
	++state.blockCount;
	CurrentBlocks(state).push_back(std::move(block));
}

void AppendTableBlock(
		BlockParseState &state,
		HtmlTable table,
		QString anchorId) {
	FlushLeafBlock(state);
	if (!EmitBlockAllowed(state)) {
		return;
	}
	EnsureListItemTarget(state);
	if (table.truncated) {
		state.truncated = true;
	}
	auto block = HtmlBlock();
	block.kind = HtmlBlockKind::Table;
	block.anchorId = std::move(anchorId);
	block.table = std::move(table);
	++state.blockCount;
	CurrentBlocks(state).push_back(std::move(block));
}

void ApplyCheckboxInput(
		BlockParseState &state,
		const std::vector<HtmlAttribute> &attributes) {
	const auto type = AttributeValue(attributes, u"type"_q);
	if (!type
		|| type->trimmed().compare(u"checkbox"_q, Qt::CaseInsensitive)) {
		return;
	}
	for (auto i = int(state.stack.size()); i != 0; --i) {
		auto &container = state.stack[i - 1];
		if (container.isListItem) {
			if (container.taskState == HtmlTaskState::None) {
				container.taskState = HasAttribute(attributes, u"checked"_q)
					? HtmlTaskState::Checked
					: HtmlTaskState::Unchecked;
			}
			return;
		} else if (IsListContainer(container)) {
			return;
		}
	}
}

[[nodiscard]] QString CodeLanguageFromAttributes(
		const std::vector<HtmlAttribute> &attributes) {
	const auto value = AttributeValue(attributes, u"class"_q);
	if (!value) {
		return QString();
	}
	static const auto kPrefixes = std::array{
		u"language-"_q,
		u"lang-"_q,
		u"highlight-source-"_q,
	};
	const auto parts = value->split(
		QChar(' '),
		Qt::SkipEmptyParts);
	for (const auto &part : parts) {
		for (const auto &prefix : kPrefixes) {
			if (!part.startsWith(prefix, Qt::CaseInsensitive)) {
				continue;
			}
			const auto language = part.mid(prefix.size());
			const auto good = !language.isEmpty()
				&& (language.size() <= 20)
				&& std::all_of(
					language.begin(),
					language.end(),
					[](QChar ch) {
						return ch.isLetterOrNumber()
							|| (ch == '+')
							|| (ch == '#')
							|| (ch == '.')
							|| (ch == '_')
							|| (ch == '-');
					});
			if (good) {
				return language.toLower();
			}
		}
	}
	return QString();
}

void ProcessBlockTag(
		BlockParseState &state,
		const QString &name,
		const std::vector<HtmlAttribute> &attributes,
		bool closing,
		bool selfClosing) {
	auto &content = state.content;
	if (!content.hidden.empty()) {
		if (closing) {
			CloseHiddenElement(content, name);
		} else if (!selfClosing && !IsVoidElement(name)) {
			content.hidden.push(name);
		}
		return;
	}
	if (!closing
		&& !selfClosing
		&& !IsVoidElement(name)
		&& (IsHiddenElement(name) || IsHiddenByAttributes(attributes))) {
		content.hidden.push(name);
		return;
	}
	if (state.preDepth > 0) {
		if (name == u"pre"_q) {
			if (closing) {
				CloseStyledElement(content, name);
				UpdateActive(content.active, HtmlTag::Pre, true);
				if (--state.preDepth == 0) {
					FlushLeafBlock(state);
					state.leafKind = HtmlBlockKind::Paragraph;
					state.codeLanguage = QString();
				}
			} else if (!selfClosing) {
				UpdateActive(content.active, HtmlTag::Pre, false);
				PushStyledBlockElement(state, name, attributes);
				++state.preDepth;
			}
			return;
		}
		if (!closing
			&& (name == u"code"_q)
			&& state.codeLanguage.isEmpty()) {
			state.codeLanguage = CodeLanguageFromAttributes(attributes);
		}
		ProcessTag(content, name, attributes, closing, selfClosing);
		return;
	}
	if (!closing && (name == u"hr"_q)) {
		FlushLeafBlock(state);
		AppendDividerBlock(state);
		return;
	}
	if (name == u"pre"_q) {
		if (!closing && !selfClosing) {
			FlushLeafBlock(state);
			state.leafKind = HtmlBlockKind::Code;
			state.codeLanguage = CodeLanguageFromAttributes(attributes);
			UpdateActive(content.active, HtmlTag::Pre, false);
			PushStyledBlockElement(state, name, attributes);
			++state.preDepth;
		}
		return;
	}
	if ((name == u"blockquote"_q)
		|| (name == u"aside"_q)
		|| (name == u"footer"_q)) {
		const auto kind = (name == u"blockquote"_q)
			? HtmlBlockKind::Quote
			: (name == u"aside"_q)
			? HtmlBlockKind::Pullquote
			: HtmlBlockKind::Footer;
		if (closing) {
			if (HasOpenContainer(state, name)) {
				CloseStyledElement(content, name);
				CloseBlockContainer(state, name);
			}
		} else if (!selfClosing) {
			OpenBlockContainer(state, kind, name, attributes);
		} else {
			FlushLeafBlock(state);
		}
		return;
	}
	if ((name == u"ul"_q) || (name == u"ol"_q)) {
		if (closing) {
			if (HasOpenContainer(state, name)) {
				CloseStyledElement(content, name);
				CloseBlockContainer(state, name);
			}
		} else if (!selfClosing) {
			OpenListContainer(state, name, attributes);
		} else {
			FlushLeafBlock(state);
		}
		return;
	}
	if (name == u"details"_q) {
		if (closing) {
			if (HasOpenContainer(state, name)) {
				state.inSummary = false;
				CloseStyledElement(content, name);
				CloseBlockContainer(state, name);
			}
		} else if (!selfClosing) {
			OpenBlockContainer(
				state,
				HtmlBlockKind::Details,
				name,
				attributes);
			state.stack.back().block.detailsOpen
				= HasAttribute(attributes, u"open"_q);
		} else {
			FlushLeafBlock(state);
		}
		return;
	}
	if (name == u"summary"_q) {
		if (closing) {
			if (state.inSummary) {
				CloseStyledElement(content, name);
				FlushLeafBlock(state);
				state.inSummary = false;
			}
		} else {
			FlushLeafBlock(state);
			const auto hasDetails = [&] {
				for (const auto &container : state.stack) {
					if (container.block.kind == HtmlBlockKind::Details) {
						return true;
					}
				}
				return false;
			}();
			if (hasDetails && !selfClosing && !state.inSummary) {
				state.inSummary = true;
				PushStyledBlockElement(state, name, attributes);
			}
		}
		return;
	}
	if (name == u"li"_q) {
		ProcessListItemTag(state, attributes, closing, selfClosing);
		return;
	}
	if (!closing && (name == u"input"_q)) {
		ApplyCheckboxInput(state, attributes);
		return;
	}
	if (const auto kind = MediaKindFromName(name)) {
		if (closing) {
			return;
		} else if (!IsOwnMediaTag(attributes)
			&& !HasOpenMediaGroup(state)) {
			ProcessTag(content, name, attributes, closing, selfClosing);
			return;
		} else if (const auto formula = ImageMathFormula(attributes)) {
			if (IsInlineMathImage(attributes)) {
				UpdateActive(content.active, HtmlTag::IvMath, false);
				AppendText(content, *formula);
				UpdateActive(content.active, HtmlTag::IvMath, true);
			} else {
				AppendMathBlock(state, *formula, attributes);
			}
		} else {
			AppendMediaBlock(state, *kind, attributes);
		}
		return;
	}
	if (closing && HasOpenMediaGroupContainer(state, name)) {
		CloseStyledElement(content, name);
		CloseBlockContainer(state, name);
		return;
	}
	if (!closing) {
		const auto group = IsMediaGroupName(name)
			? std::make_optional((name == u"tg-slideshow"_q)
				? HtmlBlockKind::Slideshow
				: HtmlBlockKind::Collage)
			: std::optional<HtmlBlockKind>();
		if (group) {
			if (!selfClosing) {
				OpenBlockContainer(state, *group, name, attributes);
			} else {
				FlushLeafBlock(state);
			}
			return;
		}
	} else if (IsMediaGroupName(name)) {
		return;
	}
	if (name == u"tg-map"_q) {
		if (!closing) {
			AppendMapBlock(state, attributes);
		}
		return;
	}
	if (name == u"tg-math-block"_q) {
		FlushLeafBlock(state);
		if (closing) {
			state.leafKind = HtmlBlockKind::Paragraph;
		} else if (!selfClosing) {
			const auto &blocks = CurrentBlocks(state);
			if (!blocks.empty()
				&& (blocks.back().kind == HtmlBlockKind::Math)
				&& !blocks.back().formula.isEmpty()) {
				content.hidden.push(name);
				return;
			}
			state.leafKind = HtmlBlockKind::Math;
			state.leafAnchorId = AnchorIdFromAttributes(attributes);
		}
		return;
	}
	if (name == u"figcaption"_q) {
		if (closing) {
			if (state.inCaption) {
				CloseStyledElement(content, name);
				FlushLeafBlock(state);
				state.inCaption = false;
			}
		} else {
			FlushLeafBlock(state);
			if (!selfClosing && !state.inCaption) {
				state.inCaption = true;
				PushStyledBlockElement(state, name, attributes);
			}
		}
		return;
	}
	if (const auto level = HeadingLevelFromName(name)) {
		FlushLeafBlock(state);
		if (closing) {
			CloseStyledElement(content, name);
			state.leafKind = HtmlBlockKind::Paragraph;
			state.headingLevel = 0;
		} else {
			state.leafKind = HtmlBlockKind::Heading;
			state.headingLevel = level;
			state.leafAnchorId = AnchorIdFromAttributes(attributes);
			if (!selfClosing) {
				PushStyledBlockElement(state, name, attributes);
			}
		}
		return;
	}
	if (IsParagraphFlushBoundary(name)) {
		if (closing) {
			CloseStyledElement(content, name);
			FlushLeafBlock(state);
		} else {
			FlushLeafBlock(state);
			state.leafAnchorId = AnchorIdFromAttributes(attributes);
			if (!selfClosing) {
				PushStyledBlockElement(state, name, attributes);
			}
		}
		return;
	}
	ProcessTag(content, name, attributes, closing, selfClosing);
}

} // namespace

QString EscapeForHtml(QStringView text) {
	// Mirrors QString::toHtmlEscaped(), escaping & " < > as HTML entities.
	// We do it by hand because toHtmlEscaped() goes through
	// std::u16string_view::find_first_of(), which hangs in an infinite loop
	// on Windows ARM64 builds (a bug in the MSVC STL Neon implementation,
	// _Impl_first_neon in vector_algorithms.cpp).
	auto result = QString();
	result.reserve(text.size());
	for (const auto ch : text) {
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

std::optional<TextWithTags> TextWithTagsFromHtml(
		QStringView html,
		bool richFormatting) {
	auto parsed = ParseFragment(html, {}, nullptr, richFormatting);
	if (parsed.text.tags.isEmpty() && !parsed.removedRedundantLinks) {
		return std::nullopt;
	}
	return std::move(parsed.text);
}

TextWithTags TextWithTagsFromHtmlFragment(QStringView html) {
	return ParseFragment(html).text;
}

bool HtmlContainsTable(QStringView html) {
	return (FindTagStart(html, u"<table"_q, 0) >= 0);
}

std::optional<HtmlTable> TableFromHtml(
		QStringView html,
		const HtmlTableLimits &limits) {
	if ((limits.maxRows <= 0)
		|| (limits.maxColumns <= 0)
		|| (limits.maxCells <= 0)) {
		return std::nullopt;
	}
	const auto start = FindTagStart(html, u"<table"_q, 0);
	if (start < 0) {
		return std::nullopt;
	}
	const auto classes = ParseStyleClasses(html);
	const auto scanned = ScanTable(html, start, limits, classes);
	auto result = TableFromScan(html, scanned, limits, classes);
	if (result) {
		result->sourceFrom = start;
	}
	return result;
}

std::optional<HtmlBlocks> BlocksFromHtml(
		QStringView html,
		const HtmlBlocksLimits &limits) {
	if ((limits.maxBlocks <= 0)
		|| (limits.maxBlockLength <= 0)
		|| (limits.maxTotalLength <= 0)) {
		return std::nullopt;
	}
	const auto classes = ParseStyleClasses(html);
	auto state = BlockParseState();
	state.limits = &limits;
	state.content.classes = &classes;
	state.content.richFormatting = true;
	ScanHtml(html, [&](int from, int till) {
		if (state.content.hidden.empty()) {
			AppendText(state.content, html.mid(from, till - from));
		}
	}, [&](
			const QString &name,
			int tagFrom,
			int nameEnd,
			int tagEnd,
			bool closing,
			bool selfClosing) {
		auto attributes = std::vector<HtmlAttribute>();
		if (!closing) {
			attributes = ReadAttributes(
				html,
				nameEnd,
				tagEnd,
				state.content.entityCache);
		}
		if (!closing
			&& !selfClosing
			&& (name == u"table"_q)
			&& state.content.hidden.empty()
			&& (state.preDepth == 0)
			&& !IsHiddenByAttributes(attributes)) {
			const auto scanned = ScanTable(
				html,
				tagFrom,
				limits.table,
				classes);
			if (auto table = TableFromScan(
					html,
					scanned,
					limits.table,
					classes)) {
				table->sourceFrom = tagFrom;
				AppendTableBlock(
					state,
					std::move(*table),
					AnchorIdFromAttributes(attributes));
				return scanned.sourceTill;
			}
		}
		ProcessBlockTag(state, name, attributes, closing, selfClosing);
		return -1;
	});
	while (!state.stack.empty()) {
		PopBlockContainer(state);
	}
	FlushLeafBlock(state);
	const auto plainSingle = (state.blocks.size() == 1)
		&& (state.blocks.front().kind == HtmlBlockKind::Paragraph)
		&& !HasInlineMathTag(state.blocks.front().text.tags);
	if (state.blocks.empty() || plainSingle) {
		return std::nullopt;
	}
	return HtmlBlocks{
		.blocks = std::move(state.blocks),
		.truncated = state.truncated,
	};
}

} // namespace TextUtilities
