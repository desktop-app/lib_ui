// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/qt/qt_compare.h"
#include "base/basic_types.h"
#include "base/algorithm.h"

#include <QtCore/QList>
#include <QtCore/QVector>
#include <QtGui/QClipboard>

enum class EntityType : uchar {
	Invalid = 0,

	Url,
	CustomUrl,
	Email,
	Hashtag,
	Cashtag,
	Mention,
	MentionName,
	CustomEmoji,
	BotCommand,
	MediaTimestamp,
	Colorized, // Senders in chat list, attachments in chat list, etc.
	Phone,

	Bold,
	Semibold,
	Italic,
	Underline,
	StrikeOut,
	Code, // inline
	Pre,  // block
	Blockquote,
	Spoiler,
};

enum class EntityLinkShown : uchar {
	Full,
	Partial,
};

struct EntityLinkData {
	QString text;
	QString data;
	EntityType type = EntityType::Invalid;
	EntityLinkShown shown = EntityLinkShown::Full;

	friend inline auto operator<=>(
		const EntityLinkData &,
		const EntityLinkData &) = default;
	friend inline bool operator==(
		const EntityLinkData &,
		const EntityLinkData &) = default;
};

class EntityInText;
using EntitiesInText = QVector<EntityInText>;

class EntityInText {
public:
	EntityInText(
		EntityType type,
		int offset,
		int length,
		const QString &data = QString());

	[[nodiscard]] EntityType type() const {
		return _type;
	}
	[[nodiscard]] int offset() const {
		return _offset;
	}
	[[nodiscard]] int length() const {
		return _length;
	}
	[[nodiscard]] QString data() const {
		return _data;
	}

	void extendToLeft(int extent) {
		_offset -= extent;
		_length += extent;
	}
	void shrinkFromRight(int shrink) {
		_length -= shrink;
	}
	void shiftLeft(int shift) {
		_offset -= shift;
		if (_offset < 0) {
			_length += _offset;
			_offset = 0;
			if (_length < 0) {
				_length = 0;
			}
		}
	}
	void shiftRight(int shift) {
		_offset += shift;
	}
	void updateTextEnd(int textEnd) {
		if (_offset > textEnd) {
			_offset = textEnd;
			_length = 0;
		} else if (_offset + _length > textEnd) {
			_length = textEnd - _offset;
		}
	}

	[[nodiscard]] static int FirstMonospaceOffset(
		const EntitiesInText &entities,
		int textLength);

	explicit operator bool() const {
		return type() != EntityType::Invalid;
	}

	friend inline auto operator<=>(
		const EntityInText &,
		const EntityInText &) = default;
	friend inline bool operator==(
		const EntityInText &,
		const EntityInText &) = default;

private:
	EntityType _type = EntityType::Invalid;
	int _offset = 0;
	int _length = 0;
	QString _data;

};

struct TextWithEntities {
	QString text;
	EntitiesInText entities;

	bool empty() const {
		return text.isEmpty();
	}

	void reserve(int size, int entitiesCount = 0) {
		text.reserve(size);
		entities.reserve(entitiesCount);
	}

	TextWithEntities &append(TextWithEntities &&other) {
		const auto shift = text.size();
		for (auto &entity : other.entities) {
			entity.shiftRight(shift);
		}
		text.append(other.text);
		entities.append(other.entities);
		return *this;
	}
	TextWithEntities &append(const TextWithEntities &other) {
		const auto shift = text.size();
		text.append(other.text);
		entities.reserve(entities.size() + other.entities.size());
		for (auto entity : other.entities) {
			entity.shiftRight(shift);
			entities.append(entity);
		}
		return *this;
	}
	TextWithEntities &append(const QString &other) {
		text.append(other);
		return *this;
	}
	TextWithEntities &append(QLatin1String other) {
		text.append(other);
		return *this;
	}
	TextWithEntities &append(QChar other) {
		text.append(other);
		return *this;
	}

	static TextWithEntities Simple(const QString &simple) {
		auto result = TextWithEntities();
		result.text = simple;
		return result;
	}

	friend inline auto operator<=>(
		const TextWithEntities &,
		const TextWithEntities &) = default;
	friend inline bool operator==(
		const TextWithEntities &,
		const TextWithEntities &) = default;
};

struct TextForMimeData {
	QString expanded;
	TextWithEntities rich;

	bool empty() const {
		return expanded.isEmpty();
	}

	void reserve(int size, int entitiesCount = 0) {
		expanded.reserve(size);
		rich.reserve(size, entitiesCount);
	}
	TextForMimeData &append(TextForMimeData &&other) {
		expanded.append(other.expanded);
		rich.append(std::move(other.rich));
		return *this;
	}
	TextForMimeData &append(TextWithEntities &&other) {
		expanded.append(other.text);
		rich.append(std::move(other));
		return *this;
	}
	TextForMimeData &append(const QString &other) {
		expanded.append(other);
		rich.append(other);
		return *this;
	}
	TextForMimeData &append(QLatin1String other) {
		expanded.append(other);
		rich.append(other);
		return *this;
	}
	TextForMimeData &append(QChar other) {
		expanded.append(other);
		rich.append(other);
		return *this;
	}

	static TextForMimeData WithExpandedLinks(const TextWithEntities &text);
	static TextForMimeData Rich(TextWithEntities &&rich) {
		auto result = TextForMimeData();
		result.expanded = rich.text;
		result.rich = std::move(rich);
		return result;
	}
	static TextForMimeData Simple(const QString &simple) {
		auto result = TextForMimeData();
		result.expanded = result.rich.text = simple;
		return result;
	}
};

enum {
	TextParseMultiline = 0x001,
	TextParseLinks = 0x002,
	TextParseMentions = 0x004,
	TextParseHashtags = 0x008,
	TextParseBotCommands = 0x010,
	TextParseMarkdown = 0x020,
	TextParseColorized = 0x040,
};

struct TextWithTags {
	struct Tag {
		int offset = 0;
		int length = 0;
		QString id;

		friend inline auto operator<=>(const Tag &, const Tag &) = default;
		friend inline bool operator==(const Tag &, const Tag &) = default;
	};
	using Tags = QVector<Tag>;

	QString text;
	Tags tags;

	[[nodiscard]] bool empty() const {
		return text.isEmpty();
	}
	friend inline auto operator<=>(
		const TextWithTags &,
		const TextWithTags &) = default;
	friend inline bool operator==(
		const TextWithTags &,
		const TextWithTags &) = default;
};

// Parsing helpers.

namespace TextUtilities {

bool IsValidProtocol(const QString &protocol);
bool IsValidTopDomain(const QString &domain);

const QRegularExpression &RegExpMailNameAtEnd();
const QRegularExpression &RegExpHashtag();
const QRegularExpression &RegExpHashtagExclude();
const QRegularExpression &RegExpMention();
const QRegularExpression &RegExpBotCommand();
const QRegularExpression &RegExpDigitsExclude();
QString MarkdownBoldGoodBefore();
QString MarkdownBoldBadAfter();
QString MarkdownItalicGoodBefore();
QString MarkdownItalicBadAfter();
QString MarkdownStrikeOutGoodBefore();
QString MarkdownStrikeOutBadAfter();
QString MarkdownCodeGoodBefore();
QString MarkdownCodeBadAfter();
QString MarkdownPreGoodBefore();
QString MarkdownPreBadAfter();
QString MarkdownSpoilerGoodBefore();
QString MarkdownSpoilerBadAfter();

// Text preprocess.
QString EscapeForRichParsing(const QString &text);
QString SingleLine(const QString &text);
TextWithEntities SingleLine(const TextWithEntities &text);
QString RemoveAccents(const QString &text);
QString RemoveEmoji(const QString &text);
QStringList PrepareSearchWords(const QString &query, const QRegularExpression *SplitterOverride = nullptr);
bool CutPart(TextWithEntities &sending, TextWithEntities &left, int limit);

struct MentionNameFields {
	uint64 selfId = 0;
	uint64 userId = 0;
	uint64 accessHash = 0;
};
[[nodiscard]] MentionNameFields MentionNameDataToFields(QStringView data);
[[nodiscard]] QString MentionNameDataFromFields(
	const MentionNameFields &fields);

// New entities are added to the ones that are already in result.
// Changes text if (flags & TextParseMarkdown).
TextWithEntities ParseEntities(const QString &text, int32 flags);
void ParseEntities(TextWithEntities &result, int32 flags);

void PrepareForSending(TextWithEntities &result, int32 flags);
void Trim(TextWithEntities &result);

enum class PrepareTextOption {
	IgnoreLinks,
	CheckLinks,
};
inline QString PrepareForSending(const QString &text, PrepareTextOption option = PrepareTextOption::IgnoreLinks) {
	auto result = TextWithEntities { text };
	auto prepareFlags = (option == PrepareTextOption::CheckLinks) ? (TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands) : 0;
	PrepareForSending(result, prepareFlags);
	return result.text;
}

// Replace bad symbols with space and remove '\r'.
void ApplyServerCleaning(TextWithEntities &result);

[[nodiscard]] int SerializeTagsSize(const TextWithTags::Tags &tags);
[[nodiscard]] QByteArray SerializeTags(const TextWithTags::Tags &tags);
[[nodiscard]] TextWithTags::Tags DeserializeTags(
	QByteArray data,
	int textLength);
[[nodiscard]] QString TagsMimeType();
[[nodiscard]] QString TagsTextMimeType();

inline const auto kMentionTagStart = qstr("mention://");

[[nodiscard]] bool IsMentionLink(QStringView link);
[[nodiscard]] QString MentionEntityData(QStringView link);
[[nodiscard]] bool IsSeparateTag(QStringView tag);
[[nodiscard]] QString JoinTag(const QList<QStringView> &list);
[[nodiscard]] QList<QStringView> SplitTags(QStringView tag);
[[nodiscard]] QString TagWithRemoved(
	const QString &tag,
	const QString &removed);
[[nodiscard]] QString TagWithAdded(const QString &tag, const QString &added);
[[nodiscard]] TextWithTags::Tags SimplifyTags(TextWithTags::Tags tags);

EntitiesInText ConvertTextTagsToEntities(const TextWithTags::Tags &tags);
TextWithTags::Tags ConvertEntitiesToTextTags(
	const EntitiesInText &entities);
std::unique_ptr<QMimeData> MimeDataFromText(const TextForMimeData &text);
std::unique_ptr<QMimeData> MimeDataFromText(TextWithTags &&text);
void SetClipboardText(
	const TextForMimeData &text,
	QClipboard::Mode mode = QClipboard::Clipboard);

} // namespace TextUtilities
