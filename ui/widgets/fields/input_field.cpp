// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/fields/input_field.h"

#include "ui/text/text.h"
#include "ui/text/text_renderer.h" // kQuoteCollapsedLines
#include "ui/widgets/fields/custom_field_object.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/emoji_config.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/qt_weak_factory.h"
#include "ui/integration.h"
#include "base/invoke_queued.h"
#include "base/random.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "emoji_suggestions_helper.h"
#include "base/qthelp_regex.h"
#include "base/qt/qt_common_adapters.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtGui/QClipboard>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QRawFont>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCommonStyle>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QTextEdit>
#include <QShortcut>

namespace Ui {
namespace {

constexpr auto kInstantReplaceRandomId = QTextFormat::UserProperty;
constexpr auto kInstantReplaceWhatId = QTextFormat::UserProperty + 1;
constexpr auto kInstantReplaceWithId = QTextFormat::UserProperty + 2;
constexpr auto kReplaceTagId = QTextFormat::UserProperty + 3;
constexpr auto kTagProperty = QTextFormat::UserProperty + 4;
constexpr auto kCustomEmojiText = QTextFormat::UserProperty + 5;
constexpr auto kCustomEmojiLink = QTextFormat::UserProperty + 6;
constexpr auto kCustomEmojiId = QTextFormat::UserProperty + 7;
constexpr auto kQuoteFormatId = QTextFormat::UserProperty + 8;
constexpr auto kQuoteId = QTextFormat::UserProperty + 9;
constexpr auto kPreLanguage = QTextFormat::UserProperty + 10;
constexpr auto kCollapsedQuoteFormat = QTextFormat::UserObject + 1;
constexpr auto kCustomEmojiFormat = QTextFormat::UserObject + 2;

const auto kObjectReplacementCh = QChar(QChar::ObjectReplacementCharacter);
const auto kObjectReplacement = QString::fromRawData(
	&kObjectReplacementCh,
	1);
const auto &kTagBold = InputField::kTagBold;
const auto &kTagItalic = InputField::kTagItalic;
const auto &kTagUnderline = InputField::kTagUnderline;
const auto &kTagStrikeOut = InputField::kTagStrikeOut;
const auto &kTagCode = InputField::kTagCode;
const auto &kTagPre = InputField::kTagPre;
const auto &kTagBlockquote = InputField::kTagBlockquote;
const auto &kTagBlockquoteCollapsed = InputField::kTagBlockquoteCollapsed;
const auto &kTagSpoiler = InputField::kTagSpoiler;
const auto kTagCheckLinkMeta = u"^:/:/:^"_q;
const auto kSoftLine = QChar::LineSeparator;
const auto kHardLine = QChar::ParagraphSeparator;

// We need unique tags otherwise same custom emoji would join in a single
// QTextCharFormat with the same properties, including kCustomEmojiText.
auto GlobalCustomEmojiCounter = 0;

class InputDocument : public QTextDocument {
public:
	InputDocument(QObject *parent, const style::InputField &st);

protected:
	QVariant loadResource(int type, const QUrl &name) override;

private:
	const style::InputField &_st;
	std::map<QUrl, QVariant> _emojiCache;
	rpl::lifetime _lifetime;

};

InputDocument::InputDocument(QObject *parent, const style::InputField &st)
: QTextDocument(parent)
, _st(st) {
	Emoji::Updated(
	) | rpl::start_with_next([=] {
		_emojiCache.clear();
	}, _lifetime);
}

QVariant InputDocument::loadResource(int type, const QUrl &name) {
	if (type != QTextDocument::ImageResource
		|| name.scheme() != qstr("emoji")) {
		return QTextDocument::loadResource(type, name);
	}
	const auto i = _emojiCache.find(name);
	if (i != _emojiCache.end()) {
		return i->second;
	}
	auto result = [&] {
		if (const auto emoji = Emoji::FromUrl(name.toDisplayString())) {
			const auto height = std::max(
				_st.style.font->height * style::DevicePixelRatio(),
				Emoji::GetSizeNormal());
			return QVariant(Emoji::SinglePixmap(emoji, height));
		}
		return QVariant();
	}();
	_emojiCache.emplace(name, result);
	return result;
}

[[nodiscard]] bool IsNewline(QChar ch) {
	return (ch == '\r')
		|| (ch == '\n')
		|| (ch == QChar(0xfdd0)) // QTextBeginningOfFrame
		|| (ch == QChar(0xfdd1)) // QTextEndOfFrame
		|| (ch == QChar(QChar::ParagraphSeparator))
		|| (ch == QChar(QChar::LineSeparator));
}

[[nodiscard]] bool IsTagPre(QStringView tag) {
	return tag.startsWith(kTagPre);
}

[[nodiscard]] bool IsBlockTag(QStringView tag) {
	return (tag == kTagBlockquote)
		|| (tag == kTagBlockquoteCollapsed)
		|| IsTagPre(tag);
}

[[nodiscard]] QStringView FindBlockTag(QStringView tag) {
	for (const auto &tag : TextUtilities::SplitTags(tag)) {
		if (IsBlockTag(tag)) {
			return tag;
		}
	}
	return QStringView();
}

[[nodiscard]] bool HasBlockTag(QStringView tag) {
	return !FindBlockTag(tag).isEmpty();
}

[[nodiscard]] bool HasBlockTag(const QTextBlock &block) {
	return HasBlockTag(
		block.blockFormat().property(kQuoteFormatId).toString());
}

[[nodiscard]] bool HasSpoilerTag(QStringView tag) {
	return ranges::contains(TextUtilities::SplitTags(tag), kTagSpoiler);
}

[[nodiscard]] bool StartsWithPre(not_null<QTextDocument*> document) {
	const auto format = document->firstBlock().blockFormat();
	const auto tag = format.property(kQuoteFormatId).toString();
	return IsTagPre(FindBlockTag(tag));
}

[[nodiscard]] QString WithBlockTagRemoved(QStringView tag) {
	auto list = TextUtilities::SplitTags(tag);
	list.erase(ranges::remove_if(list, IsBlockTag), list.end());
	return list.isEmpty() ? QString() : TextUtilities::JoinTag(list);
}

[[nodiscard]] QTextBlock FindBlock(
		not_null<QTextDocument*> document,
		int id) {
	auto block = document->firstBlock();
	for (; block.isValid(); block = block.next()) {
		if (block.blockFormat().property(kQuoteId).toInt() == id) {
			return block;
		}
	}
	return QTextBlock();
}

[[nodiscard]] TextWithTags ShiftLeftBlockTag(TextWithTags text) {
	while (!text.tags.isEmpty() && text.tags.front().length <= 0) {
		text.tags.erase(text.tags.begin());
	}
	if (text.tags.isEmpty()
		|| text.empty()
		|| !IsNewline(text.text.front())) {
		return text;
	}
	auto &tag = text.tags.front();
	if (tag.offset > 0 || !HasBlockTag(tag.id)) {
		return text;
	}
	auto stripped = WithBlockTagRemoved(tag.id);
	if (tag.length == 1) {
		if (stripped.isEmpty()) {
			text.tags.erase(text.tags.begin());
		} else {
			tag.id = stripped;
		}
	} else {
		++tag.offset;
		--tag.length;
		if (!stripped.isEmpty()) {
			text.tags.insert(
				text.tags.begin(),
				TextWithTags::Tag{ 0, 1, stripped });
		}
	}
	return text;
}

[[nodiscard]] TextWithTags ShiftRightBlockTag(TextWithTags text) {
	while (!text.tags.isEmpty() && text.tags.back().length <= 0) {
		text.tags.pop_back();
	}
	if (text.tags.isEmpty()
		|| text.empty()
		|| !IsNewline(text.text.back())) {
		return text;
	}
	auto &tag = text.tags.back();
	if (tag.offset + tag.length < text.text.size() || !HasBlockTag(tag.id)) {
		return text;
	}
	auto stripped = WithBlockTagRemoved(tag.id);
	if (tag.length == 1) {
		if (stripped.isEmpty()) {
			text.tags.pop_back();
		} else {
			tag.id = stripped;
		}
	} else {
		--tag.length;
		if (!stripped.isEmpty()) {
			text.tags.push_back({ int(text.text.size()) - 1, 1, stripped });
		}
	}
	return text;
}

[[nodiscard]] bool IsValidMarkdownLink(QStringView link) {
	return (link.indexOf('.') >= 0) || (link.indexOf(':') >= 0);
}

[[nodiscard]] bool IsCustomEmojiLink(QStringView link) {
	return link.startsWith(InputField::kCustomEmojiTagStart);
}

[[nodiscard]] QString MakeUniqueCustomEmojiLink(QStringView link) {
	if (!IsCustomEmojiLink(link)) {
		return link.toString();
	}
	const auto index = link.indexOf('?');
	return u"%1?%2"_q
		.arg((index < 0) ? link : base::StringViewMid(link, 0, index))
		.arg(++GlobalCustomEmojiCounter);
}

[[nodiscard]] QString DefaultTagMimeProcessor(QStringView mimeTag) {
	// By default drop formatting in InputField-s.
	return {};
}

[[nodiscard]] uint64 CustomEmojiIdFromLink(QStringView link) {
	const auto skip = InputField::kCustomEmojiTagStart.size();
	const auto index = link.indexOf('?', skip + 1);
	return base::StringViewMid(
		link,
		skip,
		(index <= skip) ? -1 : (index - skip)
	).toULongLong();
}

[[nodiscard]] QString CheckFullTextTag(
		const TextWithTags &textWithTags,
		const QString &tag) {
	auto resultLink = QString();
	const auto checkingLink = (tag == kTagCheckLinkMeta);
	const auto &text = textWithTags.text;
	auto from = 0;
	auto till = int(text.size());
	const auto adjust = [&] {
		for (; from != till; ++from) {
			if (!IsNewline(text[from]) && !Text::IsSpace(text[from])) {
				break;
			}
		}
	};
	for (const auto &existing : textWithTags.tags) {
		adjust();
		if (existing.offset > from) {
			return QString();
		}
		auto found = false;
		for (const auto &single : TextUtilities::SplitTags(existing.id)) {
			const auto normalized = IsTagPre(single)
				? QStringView(kTagCode)
				: single;
			if (checkingLink && IsValidMarkdownLink(single)) {
				if (resultLink.isEmpty()) {
					resultLink = single.toString();
					found = true;
					break;
				} else if (QStringView(resultLink) == single) {
					found = true;
					break;
				}
				return QString();
			} else if (!checkingLink && QStringView(tag) == normalized) {
				found = true;
				break;
			}
		}
		if (!found) {
			return QString();
		}
		from = std::clamp(existing.offset + existing.length, from, till);
	}
	while (till != from) {
		if (!IsNewline(text[till - 1]) && !Text::IsSpace(text[till - 1])) {
			break;
		}
		--till;
	}
	return (from < till) ? QString() : checkingLink ? resultLink : tag;
}

[[nodiscard]] bool HasFullTextTag(
		const TextWithTags &textWithTags,
		const QString &tag) {
	return !CheckFullTextTag(textWithTags, tag).isEmpty();
}

[[nodiscard]] QString ReadPreLanguageName(
		const QString &text,
		int preStart,
		int preLength) {
	auto view = QStringView(text).mid(preStart, preLength);
	static const auto expression = QRegularExpression(
		"^([a-zA-Z0-9\\+\\-]+)[\\r\\n]");
	const auto m = expression.match(view);
	return m.hasMatch() ? m.captured(1).toLower() : QString();
}

class RangeAccumulator {
public:
	explicit RangeAccumulator(std::vector<InputFieldTextRange> &ranges)
	: _ranges(ranges) {
	}
	~RangeAccumulator() {
		finish();
	}

	void add(int offset, int length) {
		if (_count > 0 && _ranges[_count - 1].till >= offset) {
			accumulate_max(_ranges[_count - 1].till, offset + length);
			return;
		}
		if (_count == _ranges.size()) {
			_ranges.push_back({ offset, offset + length });
		} else {
			_ranges[_count] = { offset, offset + length };
		}
		++_count;
	}

	void finish() {
		if (_count < _ranges.size()) {
			_ranges.resize(_count);
		}
	}

private:
	std::vector<InputFieldTextRange> &_ranges;
	int _count = 0;

};

class TagAccumulator {
public:
	explicit TagAccumulator(TextWithTags::Tags &tags) : _tags(tags) {
	}

	[[nodiscard]] bool changed() const {
		return _changed;
	}
	[[nodiscard]] QString currentTag() const {
		return _currentTagId;
	}

	void feed(const QString &randomTagId, int currentPosition) {
		if (randomTagId == _currentTagId) {
			return;
		}

		if (!_currentTagId.isEmpty()) {
			const auto tag = TextWithTags::Tag{
				_currentStart,
				currentPosition - _currentStart,
				_currentTagId
			};
			if (tag.length > 0) {
				if (_currentTag >= _tags.size()) {
					_changed = true;
					_tags.push_back(tag);
				} else if (_tags[_currentTag] != tag) {
					_changed = true;
					_tags[_currentTag] = tag;
				}
				++_currentTag;
			}
		}
		_currentTagId = randomTagId;
		_currentStart = currentPosition;
	}

	void finish() {
		if (_currentTag < _tags.size()) {
			_tags.resize(_currentTag);
			_changed = true;
		}
	}

private:
	TextWithTags::Tags &_tags;
	bool _changed = false;

	int _currentTag = 0;
	int _currentStart = 0;
	QString _currentTagId;

};

struct TagStartExpression {
	QString tag;
	QString goodBefore;
	QString badAfter;
	QString badBefore;
	QString goodAfter;
};

constexpr auto kTagBoldIndex = 0;
constexpr auto kTagItalicIndex = 1;
//constexpr auto kTagUnderlineIndex = 2;
constexpr auto kTagStrikeOutIndex = 2;
constexpr auto kTagCodeIndex = 3;
constexpr auto kTagPreIndex = 4;
constexpr auto kTagSpoilerIndex = 5;
constexpr auto kInvalidPosition = std::numeric_limits<int>::max() / 2;

class TagSearchItem {
public:
	enum class Edge {
		Open,
		Close,
	};

	int matchPosition(Edge edge) const {
		return (_position >= 0) ? _position : kInvalidPosition;
	}

	void applyOffset(int offset) {
		if (_position < offset) {
			_position = -1;
		}
		accumulate_max(_offset, offset);
	}

	void fill(
			const QString &text,
			Edge edge,
			const TagStartExpression &expression) {
		const auto length = text.size();
		const auto &tag = expression.tag;
		const auto tagLength = tag.size();
		const auto isGoodBefore = [&](QChar ch) {
			return expression.goodBefore.isEmpty()
				|| (expression.goodBefore.indexOf(ch) >= 0);
		};
		const auto isBadAfter = [&](QChar ch) {
			return !expression.badAfter.isEmpty()
				&& (expression.badAfter.indexOf(ch) >= 0);
		};
		const auto isBadBefore = [&](QChar ch) {
			return !expression.badBefore.isEmpty()
				&& (expression.badBefore.indexOf(ch) >= 0);
		};
		const auto isGoodAfter = [&](QChar ch) {
			return expression.goodAfter.isEmpty()
				|| (expression.goodAfter.indexOf(ch) >= 0);
		};
		const auto check = [&](Edge edge) {
			if (_position > 0) {
				const auto before = text[_position - 1];
				if ((edge == Edge::Open && !isGoodBefore(before))
					|| (edge == Edge::Close && isBadBefore(before))) {
					return false;
				}
			}
			if (_position + tagLength < length) {
				const auto after = text[_position + tagLength];
				if ((edge == Edge::Open && isBadAfter(after))
					|| (edge == Edge::Close && !isGoodAfter(after))) {
					return false;
				}
			}
			return true;
		};
		const auto edgeIndex = static_cast<int>(edge);
		if (_position >= 0) {
			if (_checked[edgeIndex]) {
				return;
			} else if (check(edge)) {
				_checked[edgeIndex] = true;
				return;
			} else {
				_checked = { { false, false } };
			}
		}
		while (true) {
			_position = text.indexOf(tag, _offset);
			if (_position < 0) {
				_offset = _position = kInvalidPosition;
				break;
			}
			_offset = _position + tagLength;
			if (check(edge)) {
				break;
			} else {
				continue;
			}
		}
		if (_position == kInvalidPosition) {
			_checked = { { true, true } };
		} else {
			_checked = { { false, false } };
			_checked[edgeIndex] = true;
		}
	}

private:
	int _offset = 0;
	int _position = -1;
	std::array<bool, 2> _checked = { { false, false } };

};

const std::vector<TagStartExpression> &TagStartExpressions() {
	static auto cached = std::vector<TagStartExpression> {
		{
			kTagBold,
			TextUtilities::MarkdownBoldGoodBefore(),
			TextUtilities::MarkdownBoldBadAfter(),
			TextUtilities::MarkdownBoldBadAfter(),
			TextUtilities::MarkdownBoldGoodBefore()
		},
		{
			kTagItalic,
			TextUtilities::MarkdownItalicGoodBefore(),
			TextUtilities::MarkdownItalicBadAfter(),
			TextUtilities::MarkdownItalicBadAfter(),
			TextUtilities::MarkdownItalicGoodBefore()
		},
		//{
		//	kTagUnderline,
		//	TextUtilities::MarkdownUnderlineGoodBefore(),
		//	TextUtilities::MarkdownUnderlineBadAfter(),
		//	TextUtilities::MarkdownUnderlineBadAfter(),
		//	TextUtilities::MarkdownUnderlineGoodBefore()
		//},
		{
			kTagStrikeOut,
			TextUtilities::MarkdownStrikeOutGoodBefore(),
			TextUtilities::MarkdownStrikeOutBadAfter(),
			TextUtilities::MarkdownStrikeOutBadAfter(),
			QString(),
		},
		{
			kTagCode,
			TextUtilities::MarkdownCodeGoodBefore(),
			TextUtilities::MarkdownCodeBadAfter(),
			TextUtilities::MarkdownCodeBadAfter(),
			TextUtilities::MarkdownCodeGoodBefore()
		},
		{
			kTagPre,
			TextUtilities::MarkdownPreGoodBefore(),
			TextUtilities::MarkdownPreBadAfter(),
			TextUtilities::MarkdownPreBadAfter(),
			TextUtilities::MarkdownPreGoodBefore()
		},
		{
			kTagSpoiler,
			TextUtilities::MarkdownSpoilerGoodBefore(),
			TextUtilities::MarkdownSpoilerBadAfter(),
			TextUtilities::MarkdownSpoilerBadAfter(),
			TextUtilities::MarkdownSpoilerGoodBefore()
		},
	};
	return cached;
}

const std::map<QString, int> &TagIndices() {
	static auto cached = std::map<QString, int> {
		{ kTagBold, kTagBoldIndex },
		{ kTagItalic, kTagItalicIndex },
		//{ kTagUnderline, kTagUnderlineIndex },
		{ kTagStrikeOut, kTagStrikeOutIndex },
		{ kTagCode, kTagCodeIndex },
		{ kTagPre, kTagPreIndex },
		{ kTagSpoiler, kTagSpoilerIndex },
	};
	return cached;
}

bool DoesTagFinishByNewline(const QString &tag) {
	return (tag == kTagCode);
}

class MarkdownTagAccumulator {
public:
	using Edge = TagSearchItem::Edge;

	MarkdownTagAccumulator(std::vector<InputField::MarkdownTag> *tags)
	: _tags(tags)
	, _expressions(TagStartExpressions())
	, _tagIndices(TagIndices())
	, _items(_expressions.size()) {
	}

	// Here we use the fact that text either contains only emoji
	// { adjustedTextLength = text.size() * (emojiLength - 1) }
	// or contains no emoji at all and can have tag edges in the middle
	// { adjustedTextLength = 0 }.
	//
	// Otherwise we would have to pass emoji positions inside text.
	void feed(
			const QString &text,
			int adjustedTextLength,
			const QString &textTag) {
		if (!_tags) {
			return;
		}
		const auto guard = gsl::finally([&] {
			_currentInternalLength += text.size();
			_currentAdjustedLength += adjustedTextLength;
		});
		if (!textTag.isEmpty()) {
			finishTags();
			return;
		}
		for (auto &item : _items) {
			item = TagSearchItem();
		}
		auto tryFinishTag = _currentTag;
		while (true) {
			for (; tryFinishTag != _currentFreeTag; ++tryFinishTag) {
				auto &tag = (*_tags)[tryFinishTag];
				if (tag.internalLength >= 0) {
					continue;
				}

				const auto i = _tagIndices.find(tag.tag);
				Assert(i != end(_tagIndices));
				const auto tagIndex = i->second;

				const auto atLeastOffset =
					tag.internalStart
					+ tag.tag.size()
					+ 1
					- _currentInternalLength;
				_items[tagIndex].applyOffset(atLeastOffset);

				fillItem(
					tagIndex,
					text,
					Edge::Close);
				if (finishByNewline(tryFinishTag, text, tagIndex)) {
					continue;
				}
				const auto position = matchPosition(tagIndex, Edge::Close);
				if (position < kInvalidPosition) {
					const auto till = position + tag.tag.size();
					finishTag(tryFinishTag, till, true);
					_items[tagIndex].applyOffset(till);
				}
			}
			for (auto i = 0, count = int(_items.size()); i != count; ++i) {
				fillItem(i, text, Edge::Open);
			}
			const auto min = minIndex(Edge::Open);
			if (min < 0) {
				return;
			}
			startTag(matchPosition(min, Edge::Open), _expressions[min].tag);
		}
	}

	void finish() {
		if (!_tags) {
			return;
		}
		finishTags();
		if (_currentTag < _tags->size()) {
			_tags->resize(_currentTag);
		}
	}

private:
	void finishTag(int index, int offsetFromAccumulated, bool closed) {
		Expects(_tags != nullptr);
		Expects(index >= 0 && index < _tags->size());

		auto &tag = (*_tags)[index];
		if (tag.internalLength < 0) {
			tag.internalLength = _currentInternalLength
				+ offsetFromAccumulated
				- tag.internalStart;
			tag.adjustedLength = _currentAdjustedLength
				+ offsetFromAccumulated
				- tag.adjustedStart;
			tag.closed = closed;
		}
		if (index == _currentTag) {
			++_currentTag;
		}
	}
	bool finishByNewline(
			int index,
			const QString &text,
			int tagIndex) {
		Expects(_tags != nullptr);
		Expects(index >= 0 && index < _tags->size());

		auto &tag = (*_tags)[index];

		if (!DoesTagFinishByNewline(tag.tag)) {
			return false;
		}
		const auto endPosition = newlinePosition(
			text,
			std::max(0, tag.internalStart + 1 - _currentInternalLength));
		if (matchPosition(tagIndex, Edge::Close) <= endPosition) {
			return false;
		}
		finishTag(index, endPosition, false);
		return true;
	}
	void finishTags() {
		while (_currentTag != _currentFreeTag) {
			finishTag(_currentTag, 0, false);
		}
	}
	void startTag(int offsetFromAccumulated, const QString &tag) {
		Expects(_tags != nullptr);

		const auto newTag = InputField::MarkdownTag{
			_currentInternalLength + offsetFromAccumulated,
			-1,
			_currentAdjustedLength + offsetFromAccumulated,
			-1,
			false,
			tag
		};
		if (_currentFreeTag < _tags->size()) {
			(*_tags)[_currentFreeTag] = newTag;
		} else {
			_tags->push_back(newTag);
		}
		++_currentFreeTag;
	}
	void fillItem(int index, const QString &text, Edge edge) {
		Expects(index >= 0 && index < _items.size());

		_items[index].fill(text, edge, _expressions[index]);
	}
	int matchPosition(int index, Edge edge) const {
		Expects(index >= 0 && index < _items.size());

		return _items[index].matchPosition(edge);
	}
	int newlinePosition(const QString &text, int offset) const {
		const auto length = text.size();
		if (offset < length) {
			const auto begin = text.data();
			const auto end = begin + length;
			for (auto ch = begin + offset; ch != end; ++ch) {
				if (IsNewline(*ch)) {
					return (ch - begin);
				}
			}
		}
		return kInvalidPosition;
	}
	int minIndex(Edge edge) const {
		auto result = -1;
		auto minPosition = kInvalidPosition;
		for (auto i = 0, count = int(_items.size()); i != count; ++i) {
			const auto position = matchPosition(i, edge);
			if (position < minPosition) {
				minPosition = position;
				result = i;
			}
		}
		return result;
	}
	int minIndexForFinish(const std::vector<int> &indices) const {
		const auto tagIndex = indices[0];
		auto result = -1;
		auto minPosition = kInvalidPosition;
		for (auto i : indices) {
			const auto edge = (i == tagIndex) ? Edge::Close : Edge::Open;
			const auto position = matchPosition(i, edge);
			if (position < minPosition) {
				minPosition = position;
				result = i;
			}
		}
		return result;
	}

	std::vector<InputField::MarkdownTag> *_tags = nullptr;
	const std::vector<TagStartExpression> &_expressions;
	const std::map<QString, int> &_tagIndices;
	std::vector<TagSearchItem> _items;

	int _currentTag = 0;
	int _currentFreeTag = 0;
	int _currentInternalLength = 0;
	int _currentAdjustedLength = 0;

};

template <typename Iterator>
QString AccumulateText(Iterator begin, Iterator end) {
	auto result = QString();
	result.reserve(end - begin);
	for (auto i = end; i != begin;) {
		result.push_back(*--i);
	}
	return result;
}

QTextImageFormat PrepareEmojiFormat(EmojiPtr emoji, int lineHeight) {
	const auto factor = style::DevicePixelRatio();
	const auto size = Emoji::GetSizeNormal();
	const auto width = size + st::emojiPadding * factor * 2;
	const auto height = std::max(lineHeight * factor, size);
	auto result = QTextImageFormat();
	result.setWidth(width / factor);
	result.setHeight(height / factor);
	result.setName(emoji->toUrl());
	result.setVerticalAlignment(QTextCharFormat::AlignTop);
	return result;
}

[[nodiscard]] QTextCharFormat PrepareTagFormat(
		const style::InputField &st,
		QStringView tag) {
	auto result = QTextCharFormat();
	auto font = st.style.font;
	auto color = std::optional<QColor>();
	auto bg = std::optional<QColor>();
	auto replaceWhat = QString();
	auto replaceWith = QString();
	const auto applyOne = [&](QStringView tag) {
		if (IsCustomEmojiLink(tag)) {
			replaceWhat = tag.toString();
			replaceWith = MakeUniqueCustomEmojiLink(tag);
			result.setObjectType(kCustomEmojiFormat);
			result.setProperty(kCustomEmojiLink, replaceWith);
			result.setProperty(
				kCustomEmojiId,
				CustomEmojiIdFromLink(replaceWith));
			result.setVerticalAlignment(QTextCharFormat::AlignTop);
		} else if (IsValidMarkdownLink(tag)) {
			color = st::defaultTextPalette.linkFg->c;
		} else if (tag == kTagBold) {
			font = font->bold();
		} else if (tag == kTagItalic) {
			font = font->italic();
		} else if (tag == kTagUnderline) {
			font = font->underline();
		} else if (tag == kTagStrikeOut) {
			font = font->strikeout();
		} else if (tag == kTagCode || IsTagPre(tag)) {
			color = st::defaultTextPalette.monoFg->c;
			font = font->monospace();
		}
	};
	for (const auto &tag : TextUtilities::SplitTags(tag)) {
		applyOne(tag);
	}
	result.setFont(font);
	result.setForeground(color.value_or(st.textFg->c));
	auto value = tag.toString();
	result.setProperty(
		kTagProperty,
		(replaceWhat.isEmpty()
			? value
			: value.replace(replaceWhat, replaceWith)));
	if (bg) {
		result.setBackground(*bg);
	} else {
		result.setBackground(QBrush());
	}
	return result;
}

[[nodiscard]] int CollapsedQuoteCutoff(const style::InputField &st) {
	return (Text::kQuoteCollapsedLines + 0.8) * st.style.font->height;
}

void SetBlockMargins(QTextBlockFormat &format, const style::QuoteStyle &st) {
	format.setLeftMargin(st.padding.left());
	format.setTopMargin(st.padding.top()
		+ st.header
		+ st.verticalSkip
		+ st.verticalSkip // Those are overlapping margins, not paddings :(
		+ st.padding.bottom());
	format.setRightMargin(st.padding.right());
	format.setBottomMargin(st.padding.bottom()
		+ st.verticalSkip
		+ st.verticalSkip // Those are overlapping margins, not paddings :(
		+ st.padding.top());
}

[[nodiscard]] QRect ExtendForPaint(QRect rect, const style::QuoteStyle &st) {
	return rect.marginsAdded(
		st.padding + QMargins(0, st.header, 0, 0)
	).translated(st.padding.left(), 0);
}

[[nodiscard]] QTextBlockFormat PrepareBlockFormat(
		const style::InputField &st,
		QStringView tag = {},
		int quoteId = -1) {
	static auto AutoincrementId = 0;
	auto result = QTextBlockFormat();
	if (tag != kTagBlockquoteCollapsed) {
		result.setLineHeight(
			st.style.font->height,
			QTextBlockFormat::FixedHeight);
	}
	const auto id = (quoteId < 0) ? ++AutoincrementId : quoteId;
	if (tag == kTagBlockquote || tag == kTagBlockquoteCollapsed) {
		result.setProperty(kQuoteFormatId, tag.toString());
		result.setProperty(kQuoteId, id);
		SetBlockMargins(result, st.style.blockquote);
	} else if (IsTagPre(tag)) {
		result.setProperty(kQuoteFormatId, tag.toString());
		result.setProperty(kQuoteId, id);
		result.setProperty(kPreLanguage, tag.mid(kTagPre.size()).toString());
		SetBlockMargins(result, st.style.pre);
	}
	return result;
}

void RemoveDocumentTags(
		const style::InputField &st,
		not_null<QTextDocument*> document,
		int from,
		int end) {
	auto cursor = QTextCursor(document);
	auto blocksCheckedTill = from;
	while (blocksCheckedTill < end) {
		auto block = document->findBlock(blocksCheckedTill);
		const auto till = block.position() + block.length();
		if (from <= block.position() && till <= end + 1) {
			const auto format = block.blockFormat();
			const auto id = format.property(kQuoteFormatId).toString();
			if (!id.isEmpty()) {
				cursor.setPosition(blocksCheckedTill);
				cursor.setBlockFormat(PrepareBlockFormat(st));
			}
		}
		blocksCheckedTill = till;
	}
	cursor.setPosition(from);
	cursor.setPosition(end, QTextCursor::KeepAnchor);

	auto format = QTextCharFormat();
	format.setProperty(kTagProperty, QString());
	format.setProperty(kReplaceTagId, QString());
	format.setForeground(st.textFg);
	format.setBackground(QBrush());
	format.setFont(st.style.font);
	cursor.mergeCharFormat(format);
}

[[nodiscard]] QString TagWithoutCustomEmoji(QStringView tag) {
	auto tags = TextUtilities::SplitTags(tag);
	for (auto i = tags.begin(); i != tags.end();) {
		if (IsCustomEmojiLink(*i)) {
			i = tags.erase(i);
		} else {
			++i;
		}
	}
	return TextUtilities::JoinTag(tags);
}

void RemoveCustomEmojiTag(
		const style::InputField &st,
		not_null<QTextDocument*> document,
		const QString &existingTags,
		int from,
		int end) {
	auto cursor = QTextCursor(document);
	cursor.setPosition(from);
	cursor.setPosition(end, QTextCursor::KeepAnchor);

	auto format = PrepareTagFormat(st, TagWithoutCustomEmoji(existingTags));
	format.setProperty(kCustomEmojiLink, QString());
	format.setProperty(kCustomEmojiId, QString());
	cursor.mergeCharFormat(format);
}

void ApplyTagFormat(QTextCharFormat &to, const QTextCharFormat &from) {
	if (from.hasProperty(kTagProperty)) {
		to.setProperty(
			kTagProperty,
			TagWithoutCustomEmoji(from.property(kTagProperty).toString()));
	}
	to.setProperty(kReplaceTagId, from.property(kReplaceTagId));
	to.setFont(from.font());
	if (from.hasProperty(QTextFormat::ForegroundBrush)) {
		to.setForeground(from.brushProperty(QTextFormat::ForegroundBrush));
	}
	if (from.hasProperty(QTextFormat::BackgroundBrush)) {
		to.setBackground(from.brushProperty(QTextFormat::BackgroundBrush));
	}
}

[[nodiscard]] bool IsCollapsedQuoteFragment(const QTextFragment &fragment) {
	return (fragment.charFormat().objectType() == kCollapsedQuoteFormat)
		&& (fragment.text() == kObjectReplacement);
}

[[nodiscard]] int FindCollapsedQuoteObject(const QTextBlock &block) {
	for (auto fragmentIt = block.begin(); !fragmentIt.atEnd(); ++fragmentIt) {
		auto fragment = fragmentIt.fragment();
		if (IsCollapsedQuoteFragment(fragment)) {
			return fragment.position();
		}
	}
	return -1;
}

[[nodiscard]] TextWithTags PrepareForInsert(TextWithTags data) {
	auto &text = data.text;
	auto length = int(data.text.size());
	const auto newline = [&](int position) {
		return IsNewline(text.at(position));
	};
	const auto force = [&](int position, QChar ch) {
		if (text.at(position) != ch) {
			// Don't detach unnecessary.
			text[position] = ch;
		}
	};
	for (auto i = data.tags.begin(); i != data.tags.end();) {
		auto &tagStart = *i;
		const auto &id = tagStart.id;
		auto from = std::min(tagStart.offset, length);
		auto till = std::min(tagStart.offset + tagStart.length, length);
		if (from >= till) {
			i = data.tags.erase(i);
			continue;
		}
		const auto block = FindBlockTag(id);
		if (block.isEmpty()) {
			++i;
			continue;
		}
		auto j = i + 1;
		for (; j != data.tags.end(); ++j) {
			auto &next = *j;
			if (next.offset > till || FindBlockTag(next.id) != block) {
				break;
			}
			till = std::min(next.offset + next.length, length);
		}
		auto &tagEnd = *(j - 1);

		// Multiple lines in the same formatting tag belong to the same block
		for (auto c = from; c < till; ++c) {
			if (newline(c)) {
				force(c, kSoftLine);
			}
		}

		if (from > 0 && newline(from - 1)) {
			force(from - 1, kHardLine);
		} else if (newline(from)) {
			force(from, kHardLine);
			++from;
			++tagStart.offset;
		} else if (from > 0) {
			data.text.insert(from, kHardLine);
			++length;

			for (auto &tag : data.tags) {
				if (tag.offset >= from) {
					++tag.offset;
				} else if (tag.offset + tag.length > from) {
					++tag.length;
				}
			}
			++from;
			++till;
		}
		if (till < length && newline(till)) {
			force(till, kHardLine);
		} else if (newline(till - 1)) {
			force(till - 1, kHardLine);
			--till;
			--tagEnd.length;
		} else if (till < length) {
			data.text.insert(till, kHardLine);
			++length;

			for (auto &tag : data.tags) {
				if (tag.offset >= till) {
					++tag.offset;
				} else if (tag.offset + tag.length > till) {
					++tag.length;
				}
			}
		}
		i = j;
	}
	return data;
}

[[nodiscard]] QString FullTag(
		const QTextCharFormat &ch,
		const QTextBlockFormat &block) {
	auto simple = WithBlockTagRemoved(ch.property(kTagProperty).toString());
	auto quote = block.property(kQuoteFormatId).toString();
	return quote.isEmpty()
		? simple
		: simple.isEmpty()
		? quote
		: TextUtilities::TagWithAdded(simple, quote);
}

[[nodiscard]] TextWithTags WrapInQuote(
		TextWithTags text,
		const QString &blockTag) {
	auto from = 0, till = int(text.text.size());
	for (auto i = text.tags.begin(); from < till;) {
		if (i == text.tags.end()) {
			if (from < till) {
				text.tags.push_back({ from, till - from, blockTag });
			}
			break;
		} else if (i->offset > from) {
			i = text.tags.insert(
				i,
				{ from, i->offset - from, blockTag });
			++i;
		}
		i->id = TextUtilities::TagWithAdded(i->id, blockTag);
		from = i->offset + i->length;
		++i;
	}
	return text;
}

// Returns the position of the first inserted tag or "changedEnd" value if none found.
int ProcessInsertedTags(
		const style::InputField &st,
		not_null<QTextDocument*> document,
		int changedPosition,
		int changedEnd,
		const TextWithTags::Tags &tags,
		bool tagsReplaceExisting,
		Fn<QString(QStringView)> processor) {
	auto firstTagStart = changedEnd;
	auto applyNoTagFrom = tagsReplaceExisting ? changedPosition : changedEnd;
	for (const auto &tag : tags) {
		int tagFrom = changedPosition + tag.offset;
		int tagTo = tagFrom + tag.length;
		accumulate_max(tagFrom, changedPosition);
		accumulate_min(tagTo, changedEnd);
		auto tagId = processor ? processor(tag.id) : tag.id;
		if (tagTo > tagFrom && !tagId.isEmpty()) {
			accumulate_min(firstTagStart, tagFrom);

			PrepareFormattingOptimization(document);

			if (applyNoTagFrom < tagFrom) {
				RemoveDocumentTags(
					st,
					document,
					applyNoTagFrom,
					tagFrom);
			}
			QTextCursor c(document);
			c.setPosition(tagFrom);
			c.setPosition(tagTo, QTextCursor::KeepAnchor);
			if (const auto block = FindBlockTag(tag.id); !block.isEmpty()) {
				c.setBlockFormat(PrepareBlockFormat(st, block));
			} else if (tagsReplaceExisting) {
				const auto block = c.block();
				const auto blockStart = block.position();
				if (blockStart >= changedPosition
					&& blockStart + block.length() - 1 <= changedEnd) {
					c.setBlockFormat(PrepareBlockFormat(st));
				}
			}
			c.mergeCharFormat(PrepareTagFormat(st, tagId));
			applyNoTagFrom = tagTo;
		}
	}
	if (applyNoTagFrom < changedEnd) {
		RemoveDocumentTags(st, document, applyNoTagFrom, changedEnd);
	}

	return firstTagStart;
}

// When inserting a part of text inside a tag we need to have
// a way to know if the insertion replaced the end of the tag
// or it was strictly inside (in the middle) of the tag.
bool WasInsertTillTheEndOfTag(
		QTextBlock block,
		QTextBlock::iterator fragmentIt,
		int insertionEnd) {
	const auto format = fragmentIt.fragment().charFormat();
	const auto insertTagName = format.property(kTagProperty);
	while (true) {
		for (; !fragmentIt.atEnd(); ++fragmentIt) {
			const auto fragment = fragmentIt.fragment();
			const auto position = fragment.position();
			const auto outsideInsertion = (position >= insertionEnd);
			if (outsideInsertion) {
				const auto format = fragment.charFormat();
				const auto tag = format.property(kTagProperty).toString();
				return TagWithoutCustomEmoji(tag)
					!= TagWithoutCustomEmoji(insertTagName.toString());
			}
			const auto end = position + fragment.length();
			const auto notFullFragmentInserted = (end > insertionEnd);
			if (notFullFragmentInserted) {
				return false;
			}
		}
		block = block.next();
		if (block.isValid()) {
			fragmentIt = block.begin();
		} else {
			break;
		}
	}
	// Insertion goes till the end of the text => not strictly inside a tag.
	return true;
}

struct FormattingAction {
	enum class Type {
		Invalid,
		InsertEmoji,
		InsertCustomEmoji,
		RemoveCustomEmoji,
		TildeFont,
		RemoveTag,
		RemoveNewline,
		ClearInstantReplace,
		FixLineHeight,
		FixPreTag,
		CollapseBlockquote,
		CutCollapsedBefore,
		CutCollapsedAfter,
		MakeCollapsedBlockquote,
		RemoveBlockquote,
	};

	Type type = Type::Invalid;
	EmojiPtr emoji = nullptr;
	bool isTilde = false;
	QString tildeTag;
	QString existingTags;
	QString customEmojiText;
	QString customEmojiLink;
	int intervalStart = 0;
	int intervalEnd = 0;
	int quoteId = 0;
};

} // namespace

// kTagUnderline is not used for Markdown.

const QString InputField::kTagBold = u"**"_q;
const QString InputField::kTagItalic = u"__"_q;
const QString InputField::kTagUnderline = u"^^"_q;
const QString InputField::kTagStrikeOut = u"~~"_q;
const QString InputField::kTagCode = u"`"_q;
const QString InputField::kTagPre = u"```"_q;
const QString InputField::kTagSpoiler = u"||"_q;
const QString InputField::kTagBlockquote = u">"_q;
const QString InputField::kTagBlockquoteCollapsed = u">^"_q;
const QString InputField::kCustomEmojiTagStart = u"custom-emoji://"_q;
const int InputField::kCollapsedQuoteFormat = ::Ui::kCollapsedQuoteFormat;
const int InputField::kCustomEmojiFormat = ::Ui::kCustomEmojiFormat;
const int InputField::kCustomEmojiId = ::Ui::kCustomEmojiId;
const int InputField::kCustomEmojiLink = ::Ui::kCustomEmojiLink;
const int InputField::kQuoteId = ::Ui::kQuoteId;

class InputField::Inner final : public QTextEdit {
public:
	Inner(not_null<InputField*> parent) : QTextEdit(parent) {
	}

protected:
	bool viewportEvent(QEvent *e) override {
		return outer()->viewportEventInner(e);
	}
	void focusInEvent(QFocusEvent *e) override {
		return outer()->focusInEventInner(e);
	}
	void focusOutEvent(QFocusEvent *e) override {
		return outer()->focusOutEventInner(e);
	}
	void keyPressEvent(QKeyEvent *e) override {
		return outer()->keyPressEventInner(e);
	}
	void contextMenuEvent(QContextMenuEvent *e) override {
		return outer()->contextMenuEventInner(e);
	}
	void dropEvent(QDropEvent *e) override {
		return outer()->dropEventInner(e);
	}
	void inputMethodEvent(QInputMethodEvent *e) override {
		return outer()->inputMethodEventInner(e);
	}
	void paintEvent(QPaintEvent *e) override {
		return outer()->paintEventInner(e);
	}

	void mousePressEvent(QMouseEvent *e) override {
		return outer()->mousePressEventInner(e);
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		return outer()->mouseReleaseEventInner(e);
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		return outer()->mouseMoveEventInner(e);
	}
	void leaveEvent(QEvent *e) override {
		return outer()->leaveEventInner(e);
	}

	bool canInsertFromMimeData(const QMimeData *source) const override {
		return outer()->canInsertFromMimeDataInner(source);
	}
	void insertFromMimeData(const QMimeData *source) override {
		return outer()->insertFromMimeDataInner(source);
	}
	QMimeData *createMimeDataFromSelection() const override {
		return outer()->createMimeDataFromSelectionInner();
	}

private:
	not_null<InputField*> outer() const {
		return static_cast<InputField*>(parentWidget());
	}
	friend class InputField;

};

void InsertEmojiAtCursor(QTextCursor cursor, EmojiPtr emoji) {
	const auto currentFormat = cursor.charFormat();
	const auto blockFormat = cursor.blockFormat();
	const auto type = blockFormat.lineHeightType();
	const auto height = (type == QTextBlockFormat::FixedHeight)
		? blockFormat.lineHeight()
		: QFontMetrics(cursor.charFormat().font()).height();
	auto format = PrepareEmojiFormat(emoji, height);
	ApplyTagFormat(format, currentFormat);
	cursor.insertText(kObjectReplacement, format);
}

void InsertCustomEmojiAtCursor(
		not_null<InputField*> field,
		QTextCursor cursor,
		const QString &text,
		const QString &link) {
	const auto currentFormat = cursor.charFormat();
	const auto unique = MakeUniqueCustomEmojiLink(link);
	auto format = QTextCharFormat();
	format.setObjectType(kCustomEmojiFormat);
	format.setProperty(kCustomEmojiText, text);
	format.setProperty(kCustomEmojiLink, unique);
	format.setProperty(kCustomEmojiId, CustomEmojiIdFromLink(link));
	format.setVerticalAlignment(QTextCharFormat::AlignTop);
	format.setFont(field->st().style.font);
	format.setForeground(field->st().textFg);
	format.setBackground(QBrush());
	ApplyTagFormat(format, currentFormat);
	format.setProperty(kTagProperty, TextUtilities::TagWithAdded(
		format.property(kTagProperty).toString(),
		unique));
	cursor.insertText(kObjectReplacement, format);
}

[[nodiscard]] QTextCharFormat PrepareCollapsedQuoteFormat(int quoteId) {
	auto result = QTextCharFormat();
	result.setObjectType(kCollapsedQuoteFormat);
	result.setProperty(kTagProperty, kTagBlockquoteCollapsed);
	result.setProperty(kQuoteId, quoteId);
	result.setVerticalAlignment(QTextCharFormat::AlignNormal);
	return result;
}

void InstantReplaces::add(const QString &what, const QString &with) {
	auto node = &reverseMap;
	for (auto i = what.end(), b = what.begin(); i != b;) {
		node = &node->tail.emplace(*--i, Node()).first->second;
	}
	node->text = with;
	accumulate_max(maxLength, int(what.size()));
}

const InstantReplaces &InstantReplaces::Default() {
	static const auto result = [] {
		auto result = InstantReplaces();
		result.add("--", QString(1, QChar(8212)));
		result.add("<<", QString(1, QChar(171)));
		result.add(">>", QString(1, QChar(187)));
		result.add(
			":shrug:",
			QChar(175) + QString("\\_(") + QChar(12484) + ")_/" + QChar(175));
		result.add(":o ", QString(1, QChar(0xD83D)) + QChar(0xDE28));
		result.add("xD ", QString(1, QChar(0xD83D)) + QChar(0xDE06));
		const auto &replacements = Emoji::internal::GetAllReplacements();
		for (const auto &one : replacements) {
			const auto with = Emoji::QStringFromUTF16(one.emoji);
			const auto what = Emoji::QStringFromUTF16(one.replacement);
			result.add(what, with);
		}
		const auto &pairs = Emoji::internal::GetReplacementPairs();
		for (const auto &[what, index] : pairs) {
			const auto emoji = Emoji::internal::ByIndex(index);
			Assert(emoji != nullptr);
			result.add(what, emoji->text());
		}
		return result;
	}();
	return result;
}

const InstantReplaces &InstantReplaces::TextOnly() {
	static const auto result = [] {
		auto result = InstantReplaces();
		result.add("--", QString(1, QChar(8212)));
		result.add("<<", QString(1, QChar(171)));
		result.add(">>", QString(1, QChar(187)));
		result.add(
			":shrug:",
			QChar(175) + QString("\\_(") + QChar(12484) + ")_/" + QChar(175));
		return result;
	}();
	return result;
}

bool MarkdownEnabledState::disabled() const {
	return v::is<MarkdownDisabled>(data);
}

bool MarkdownEnabledState::enabledForTag(QStringView tag) const {
	const auto yes = std::get_if<MarkdownEnabled>(&data);
	return yes
		&& (yes->tagsSubset.empty() || yes->tagsSubset.contains(tag));
}

InputField::InputField(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &value)
: InputField(
	parent,
	st,
	Mode::SingleLine,
	std::move(placeholder),
	{ value, {} }) {
}

InputField::InputField(
	QWidget *parent,
	const style::InputField &st,
	Mode mode,
	rpl::producer<QString> placeholder,
	const QString &value)
: InputField(
	parent,
	st,
	mode,
	std::move(placeholder),
	{ value, {} }) {
}

InputField::InputField(
	QWidget *parent,
	const style::InputField &st,
	Mode mode,
	rpl::producer<QString> placeholder,
	const TextWithTags &value)
: RpWidget(parent)
, _st(st)
, _mode(mode)
, _minHeight(st.heightMin)
, _maxHeight(st.heightMax)
, _inner(std::make_unique<Inner>(this))
, _lastTextWithTags(value)
, _placeholderFull(std::move(placeholder)) {
	_inner->setDocument(CreateChild<InputDocument>(_inner.get(), _st));
	_inner->setAcceptRichText(false);
	resize(_st.width, _minHeight);

	{ // In case of default fonts all those should be zero.
		const auto metrics = QFontMetricsF(_st.style.font->f);
		const auto realAscent = int(base::SafeRound(metrics.ascent()));
		const auto ascentAdd = _st.style.font->ascent - realAscent;
		//const auto realHeight = int(base::SafeRound(metrics.height()));
		//const auto heightAdd = _font->height - realHeight - ascentAdd;
		//_customFontMargins = QMargins(0, ascentAdd, 0, heightAdd);
		_customFontMargins = QMargins(0, ascentAdd, 0, -ascentAdd);
		// We move _inner down by ascentAdd for the first line to look
		// at the same vertical position as in the default font.
		//
		// But we don't want to get vertical scroll in case the field
		// fits pixel-perfect with the default font, so we allow the
		// bottom margin to be the same shift, but negative.

		if (_mode != Mode::SingleLine) {
			const auto metrics = QFontMetricsF(_st.style.font->f);
			const auto leading = qMax(metrics.leading(), qreal(0.0));
			const auto adjustment = (metrics.ascent() + leading)
				- ((_st.style.font->height * 4) / 5);
			_placeholderCustomFontSkip = int(base::SafeRound(-adjustment));
		}
	}

	if (_st.textBg->c.alphaF() >= 1. && !_st.borderRadius) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	_inner->setFont(_st.style.font->f);
	_inner->setAlignment(_st.textAlign);
	if (_mode == Mode::SingleLine) {
		_inner->setWordWrapMode(QTextOption::NoWrap);
	}

	_placeholderFull.value(
	) | rpl::start_with_next([=](const QString &text) {
		refreshPlaceholder(text);
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		updatePalette();
	}, lifetime());
	{
		auto cursor = _inner->textCursor();

		_defaultCharFormat = cursor.charFormat();
		updatePalette();
		cursor.setCharFormat(_defaultCharFormat);
		cursor.setBlockFormat(PrepareBlockFormat(_st));

		_inner->setTextCursor(cursor);
	}
	_inner->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_inner->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	_inner->setFrameStyle(int(QFrame::NoFrame) | QFrame::Plain);
	_inner->viewport()->setAutoFillBackground(false);

	_inner->setContentsMargins(0, 0, 0, 0);
	_inner->document()->setDocumentMargin(0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_inner->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);

	_touchTimer.setCallback([=] { _touchRightButton = true; });

	base::qt_signal_producer(
		_inner->document(),
		&QTextDocument::contentsChange
	) | rpl::start_with_next([=](int position, int removed, int added) {
		documentContentsChanged(position, removed, added);
	}, lifetime());
	base::qt_signal_producer(
		_inner.get(),
		&QTextEdit::undoAvailable
	) | rpl::start_with_next([=](bool undoAvailable) {
		_undoAvailable = undoAvailable;
		Integration::Instance().textActionsUpdated();
	}, lifetime());
	base::qt_signal_producer(
		_inner.get(),
		&QTextEdit::redoAvailable
	) | rpl::start_with_next([=](bool redoAvailable) {
		_redoAvailable = redoAvailable;
		Integration::Instance().textActionsUpdated();
	}, lifetime());
	base::qt_signal_producer(
		_inner.get(),
		&QTextEdit::cursorPositionChanged
	) | rpl::start_with_next([=] {
		auto cursor = textCursor();
		if (!cursor.hasSelection() && !cursor.position()) {
			cursor.setCharFormat(_defaultCharFormat);
			setTextCursor(cursor);
		}
		if (_customObject) {
			_customObject->refreshSpoilerShown({
				cursor.selectionStart(),
				cursor.selectionEnd(),
			});
		}
	}, lifetime());
	base::qt_signal_producer(
		_inner.get(),
		&Inner::selectionChanged
	) | rpl::start_with_next([] {
		Integration::Instance().textActionsUpdated();
	}, lifetime());

	setupMarkdownShortcuts();

	const auto bar = _inner->verticalScrollBar();
	_scrollTop = bar->value();
	connect(bar, &QScrollBar::valueChanged, [=] {
		_scrollTop = bar->value();
	});

	setCursor(style::cur_text);
	heightAutoupdated();

	if (!value.text.isEmpty()) {
		setTextWithTags(value, HistoryAction::Clear);
	}

	startBorderAnimation();
	startPlaceholderAnimation();
	finishAnimating();
}

std::vector<InputField::MarkdownAction> InputField::MarkdownActions() {
	return {
		{ QKeySequence::Bold, kTagBold },
		{ QKeySequence::Italic, kTagItalic },
		{ QKeySequence::Underline, kTagUnderline },
		{ kStrikeOutSequence, kTagStrikeOut },
		{ kMonospaceSequence, kTagCode },
		{ kBlockquoteSequence, kTagBlockquote },
		{ kSpoilerSequence, kTagSpoiler },
		{ kClearFormatSequence, QString() },
		{ kEditLinkSequence, QString(), MarkdownActionType::EditLink },
	};
}

void InputField::setupMarkdownShortcuts() {
	for (const auto &action : MarkdownActions()) {
		auto shortcut = std::make_unique<QShortcut>(
			action.sequence,
			_inner.get(),
			nullptr,
			nullptr,
			Qt::WidgetShortcut);
		QObject::connect(shortcut.get(), &QShortcut::activated, [=] {
			executeMarkdownAction(action);
		});
		_markdownShortcuts.push_back(std::move(shortcut));
	}
}

bool InputField::executeMarkdownAction(MarkdownAction action) {
	if (_markdownEnabledState.disabled()) {
		return false;
	} else if (action.type == MarkdownActionType::EditLink) {
		if (!_editLinkCallback) {
			return false;
		}
		const auto cursor = textCursor();
		editMarkdownLink({
			cursor.selectionStart(),
			cursor.selectionEnd()
		});
	} else if (action.tag.isEmpty()) {
		clearSelectionMarkdown();
	} else if (!_markdownEnabledState.enabledForTag(action.tag)
		|| (action.tag == kTagCode
			&& !_markdownEnabledState.enabledForTag(kTagPre))) {
		return false;
	} else {
		toggleSelectionMarkdown(action.tag);
	}
	return true;
}

const rpl::variable<int> &InputField::scrollTop() const {
	return _scrollTop;
}

int InputField::scrollTopMax() const {
	return _inner->verticalScrollBar()->maximum();
}

void InputField::scrollTo(int top) {
	_inner->verticalScrollBar()->setValue(top);
}


bool InputField::menuShown() const {
	return _contextMenu != nullptr;
}

rpl::producer<bool> InputField::menuShownValue() const {
	return _menuShownChanges.events_starting_with(menuShown());
}

void InputField::setPreCache(Fn<not_null<Text::QuotePaintCache*>()> make) {
	_preCache = std::move(make);
	update();
}

void InputField::setBlockquoteCache(
		Fn<not_null<Text::QuotePaintCache*>()> make) {
	_blockquoteCache = std::move(make);
	update();
}

bool InputField::viewportEventInner(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		const auto ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			handleTouchEvent(ev);
			return false;
		}
	} else if (e->type() == QEvent::Paint && _customObject) {
		_customObject->setNow(crl::now());
	}
	return _inner->QTextEdit::viewportEvent(e);
}

void InputField::updatePalette() {
	auto p = _inner->palette();
	p.setColor(QPalette::Text, _st.textFg->c);
	p.setColor(QPalette::Highlight, st::msgInBgSelected->c);
	p.setColor(QPalette::HighlightedText, st::historyTextInFgSelected->c);
	_inner->setPalette(p);

	_defaultCharFormat.merge(PrepareTagFormat(_st, QString()));
	auto cursor = textCursor();

	const auto document = _inner->document();
	auto block = document->begin();
	const auto end = document->end();
	for (; block != end; block = block.next()) {
		auto till = block.position();
		for (auto i = block.begin(); !i.atEnd();) {
			for (; !i.atEnd(); ++i) {
				const auto fragment = i.fragment();
				if (!fragment.isValid() || fragment.position() < till) {
					continue;
				}
				till = fragment.position() + fragment.length();

				auto format = fragment.charFormat();
				const auto tag = format.property(kTagProperty).toString();
				const auto updatedFormat = PrepareTagFormat(_st, tag);
				format.setForeground(updatedFormat.foreground());
				format.setBackground(updatedFormat.background());
				cursor.setPosition(fragment.position());
				cursor.setPosition(till, QTextCursor::KeepAnchor);
				cursor.mergeCharFormat(format);
				i = block.begin();
				break;
			}
		}
	}

	cursor = textCursor();
	if (!cursor.hasSelection()) {
		auto format = cursor.charFormat();
		format.merge(PrepareTagFormat(
			_st,
			TagWithoutCustomEmoji(
				format.property(kTagProperty).toString())));
		cursor.setCharFormat(format);
		setTextCursor(cursor);
	}
}

void InputField::setExtendedContextMenu(
		rpl::producer<ExtendedContextMenu> value) {
	std::move(
		value
	) | rpl::start_with_next([=](auto pair) {
		auto &[menu, e] = pair;
		contextMenuEventInner(e.get(), std::move(menu));
	}, lifetime());
}

void InputField::setInstantReplaces(const InstantReplaces &replaces) {
	_mutableInstantReplaces = replaces;
}

void InputField::setInstantReplacesEnabled(rpl::producer<bool> enabled) {
	std::move(
		enabled
	) | rpl::start_with_next([=](bool value) {
		_instantReplacesEnabled = value;
	}, lifetime());
}

void InputField::setMarkdownReplacesEnabled(bool enabled) {
	setMarkdownReplacesEnabled(
		rpl::single(MarkdownEnabledState{ MarkdownEnabled() }));
}

void InputField::setMarkdownReplacesEnabled(
		rpl::producer<MarkdownEnabledState> enabled) {
	std::move(
		enabled
	) | rpl::start_with_next([=](MarkdownEnabledState state) {
		if (_markdownEnabledState != state) {
			_markdownEnabledState = state;
			if (_markdownEnabledState.disabled()) {
				_lastMarkdownTags = {};
			} else {
				handleContentsChanged();
			}
		}
	}, lifetime());
}

void InputField::setTagMimeProcessor(Fn<QString(QStringView)> processor) {
	_tagMimeProcessor = std::move(processor);
}

void InputField::setCustomTextContext(
		Fn<std::any(Fn<void()> repaint)> context,
		Fn<bool()> pausedEmoji,
		Fn<bool()> pausedSpoiler,
		CustomEmojiFactory factory) {
	_customObject = std::make_unique<CustomFieldObject>(
		this,
		std::move(context),
		std::move(pausedEmoji),
		std::move(pausedSpoiler),
		std::move(factory));
	_inner->document()->documentLayout()->registerHandler(
		kCustomEmojiFormat,
		_customObject.get());
	_inner->document()->documentLayout()->registerHandler(
		kCollapsedQuoteFormat,
		_customObject.get());
}

void InputField::customEmojiRepaint() {
	if (_customEmojiRepaintScheduled) {
		return;
	}
	_customEmojiRepaintScheduled = true;
	_inner->update();
}

void InputField::paintEventInner(QPaintEvent *e) {
	_customEmojiRepaintScheduled = false;
	paintQuotes(e);
	_inner->QTextEdit::paintEvent(e);
}

void InputField::paintQuotes(QPaintEvent *e) {
	if (!_blockquoteCache || !_preCache) {
		return;
	}
	const auto clip = e->rect();

	auto p = std::optional<QPainter>();
	const auto ensurePainter = [&] {
		if (!p) {
			p.emplace(_inner->viewport());
		}
	};

	auto shift = std::optional<QPoint>();
	const auto ensureShift = [&] {
		if (!shift) {
			shift.emplace(
				-_inner->horizontalScrollBar()->value(),
				-_inner->verticalScrollBar()->value());
		}
	};

	const auto document = _inner->document();
	const auto documentLayout = document->documentLayout();
	const auto collapsedCutoff = CollapsedQuoteCutoff(_st);

	auto textSpoilerIt = begin(_spoilerRangesText);
	const auto textSpoilerEnd = end(_spoilerRangesText);
	auto textSpoiler = (TextRange*)nullptr;
	const auto textSpoilerAdjust = [&](int position, int till) {
		if (position >= till) {
			textSpoiler = nullptr;
			return;
		}
		while (textSpoilerIt != textSpoilerEnd
			&& textSpoilerIt->till <= position) {
			++textSpoilerIt;
		}
		textSpoiler = (textSpoilerIt != textSpoilerEnd
			&& textSpoilerIt->from < till)
			? &*textSpoilerIt
			: nullptr;
	};

	auto emojiSpoilerIt = begin(_spoilerRangesEmoji);
	const auto emojiSpoilerEnd = end(_spoilerRangesEmoji);
	auto emojiSpoiler = (TextRange*)nullptr;
	const auto emojiSpoilerAdjust = [&](int position, int till) {
		if (position >= till) {
			emojiSpoiler = nullptr;
			return;
		}
		while (emojiSpoilerIt != emojiSpoilerEnd
			&& emojiSpoilerIt->till <= position) {
			++emojiSpoilerIt;
		}
		emojiSpoiler = (emojiSpoilerIt != emojiSpoilerEnd
			&& emojiSpoilerIt->from < till)
			? &*emojiSpoilerIt
			: nullptr;
	};

	const auto spoilersAdjust = [&](int position, int till) {
		textSpoilerAdjust(position, till);
		emojiSpoilerAdjust(position, till);
	};

	_spoilerRects.resize(0);
	auto lineStart = 0;
	const auto addSpoiler = [&](QRectF rect, bool blockquote) {
		auto normal = rect.toRect();
		if (lineStart < _spoilerRects.size()) {
			auto &last = _spoilerRects.back();
			if (last.geometry.intersects(normal)) {
				Assert(last.blockquote == blockquote);
				last.geometry = last.geometry.united(normal);
				return;
			}
		}
		_spoilerRects.push_back({ normal, blockquote });
	};
	const auto finishSpoilersLine = [&] {
		if (lineStart == _spoilerRects.size()) {
			return;
		}
		ranges::sort(
			_spoilerRects.begin() + lineStart,
			_spoilerRects.end(),
			ranges::less(),
			[](const SpoilerRect &r) { return r.geometry.x(); });
		auto i = _spoilerRects.begin() + lineStart;
		auto j = i + 1;
		while (j != end(_spoilerRects)) {
			if (i->geometry.x() + i->geometry.width() >= j->geometry.x()) {
				i->geometry = i->geometry.united(j->geometry);
				j = _spoilerRects.erase(j);
			} else {
				i = j++;
			}
		}
		lineStart = int(_spoilerRects.size());
	};

	auto block = document->firstBlock();
	while (block.isValid()) {
		auto belowClip = false;
		auto blockRect = std::optional<QRectF>();
		const auto ensureBlockRect = [&] {
			if (!blockRect) {
				blockRect.emplace(documentLayout->blockBoundingRect(block));
			}
		};

		const auto blockPosition = block.position();
		const auto format = block.blockFormat();
		const auto id = format.property(kQuoteFormatId).toString();
		const auto blockquote = (id == kTagBlockquote);
		const auto collapsed = (id == kTagBlockquoteCollapsed);
		const auto pre = !collapsed && IsTagPre(id);

		spoilersAdjust(blockPosition, blockPosition + block.length());
		if (textSpoiler || emojiSpoiler) {
			ensureShift();
			ensureBlockRect();
			const auto fullShift = blockRect->topLeft() + *shift;
			if (fullShift.y() >= clip.y() + clip.height()) {
				belowClip = true;
			} else if (fullShift.y() + blockRect->height() > clip.y()) {
				const auto blockLayout = block.layout();
				const auto lines = std::max(blockLayout->lineCount(), 0);
				for (auto i = 0; i != lines; ++i) {
					const auto line = blockLayout->lineAt(i);
					const auto top = fullShift.y() + line.y();
					const auto height = line.height();
					if (top + height <= clip.y()) {
						continue;
					} else if (top >= clip.y() + clip.height()) {
						belowClip = true;
						break;
					}
					const auto lineFrom = blockPosition + line.textStart();
					const auto lineTill = lineFrom + line.textLength();

					textSpoilerAdjust(lineFrom, lineTill);
					while (textSpoiler) {
						const auto from = std::max(textSpoiler->from, lineFrom);
						const auto runs = line.glyphRuns(
							std::max(textSpoiler->from, lineFrom) - blockPosition,
							std::min(textSpoiler->till, lineTill) - from);
						for (const auto &run : runs) {
							const auto runRect = run.boundingRect();
							addSpoiler(
								QRectF(
									fullShift.x() + runRect.x(),
									top,
									runRect.width(),
									height),
								blockquote);
						}
						textSpoilerAdjust(textSpoiler->till, lineTill);
					}

					emojiSpoilerAdjust(lineFrom, lineTill);
					while (emojiSpoiler) {
						const auto from = std::max(emojiSpoiler->from, lineFrom);
						const auto till = std::min(emojiSpoiler->till, lineTill);
						const auto x = line.cursorToX(from - blockPosition);
						const auto width = line.cursorToX(till - blockPosition) - x;
						addSpoiler(
							QRectF(
								fullShift.x() + std::min(x, x + width),
								top,
								std::abs(width),
								height),
							blockquote);
						emojiSpoilerAdjust(emojiSpoiler->till, lineTill);
					}

					finishSpoilersLine();
				}
			}
		}

		const auto st = pre
			? &_st.style.pre
			: (blockquote || collapsed)
			? &_st.style.blockquote
			: nullptr;
		if (st) {
			if (!shift) {
				shift.emplace(
					-_inner->horizontalScrollBar()->value(),
					-_inner->verticalScrollBar()->value());
			}
			if (!blockRect) {
				blockRect.emplace(documentLayout->blockBoundingRect(block));
			}
			const auto rect = blockRect->toRect();
			const auto added = pre
				? QMargins(0, 0, 0, st->verticalSkip)
				: QMargins();
			const auto target = ExtendForPaint(
				rect.marginsAdded(added),
				*st
			).translated(*shift);
			if (target.intersects(clip)) {
				ensurePainter();
				const auto cache = pre ? _preCache() : _blockquoteCache();
				const auto collapsible = !pre
					&& !collapsed
					&& (rect.height() > collapsedCutoff);
				Text::ValidateQuotePaintCache(*cache, *st);
				Text::FillQuotePaint(*p, target, *cache, *st, {
					.expandIcon = collapsed,
					.collapseIcon = collapsible,
				});
				if (!pre) {
					_blockquoteBg = cache->bg;
				}

				if (st->header > 0) {
					const auto font = _st.style.font->monospace();
					const auto topleft = target.topLeft();
					const auto position = topleft + st->headerPosition;
					const auto baseline = position + QPoint(0, font->ascent);
					p->setFont(font);
					p->setPen(st::defaultTextPalette.monoFg);
					p->drawText(
						baseline,
						format.property(kPreLanguage).toString());
				}
			}
		}
		if (belowClip) {
			break;
		}
		block = block.next();
	}
}

void InputField::setDocumentMargin(float64 margin) {
	_settingDocumentMargin = true;
	document()->setDocumentMargin(margin);
	_settingDocumentMargin = false;
}

void InputField::setAdditionalMargin(int margin) {
	setAdditionalMargins({ margin, margin, margin, margin });
}

void InputField::setAdditionalMargins(QMargins margins) {
	_additionalMargins = margins;
	QResizeEvent e(size(), size());
	QCoreApplication::sendEvent(this, &e);
}

void InputField::setMaxLength(int length) {
	if (_maxLength != length) {
		_maxLength = length;
		if (_maxLength > 0) {
			const auto document = _inner->document();
			_correcting = true;
			QTextCursor(document).joinPreviousEditBlock();
			const auto guard = gsl::finally([&] {
				_correcting = false;
				QTextCursor(document).endEditBlock();
				handleContentsChanged();
			});

			auto cursor = QTextCursor(document);
			cursor.movePosition(QTextCursor::End);
			chopByMaxLength(0, cursor.position());
		}
	}
}

void InputField::setMinHeight(int height) {
	_minHeight = height;
}

void InputField::setMaxHeight(int height) {
	_maxHeight = height;
}

void InputField::insertTag(const QString &text, QString tagId) {
	auto cursor = textCursor();
	const auto position = cursor.position();

	const auto document = _inner->document();
	auto block = document->findBlock(position);
	for (auto iter = block.begin(); !iter.atEnd(); ++iter) {
		auto fragment = iter.fragment();
		Assert(fragment.isValid());

		const auto fragmentPosition = fragment.position();
		const auto fragmentEnd = (fragmentPosition + fragment.length());
		if (fragmentPosition >= position || fragmentEnd < position) {
			continue;
		}

		const auto format = fragment.charFormat();
		if (format.isImageFormat()) {
			continue;
		}

		auto mentionInCommand = false;
		const auto fragmentText = fragment.text();
		for (auto i = position - fragmentPosition; i > 0; --i) {
			const auto previous = fragmentText[i - 1];
			if (previous == '@' || previous == '#' || previous == '/') {
				if ((i == position - fragmentPosition
					|| (previous == '/'
						? fragmentText[i].isLetterOrNumber()
						: fragmentText[i].isLetter())
					|| previous == '#') &&
					(i < 2 || !(fragmentText[i - 2].isLetterOrNumber()
						|| fragmentText[i - 2] == '_'))) {
					cursor.setPosition(fragmentPosition + i - 1);
					auto till = fragmentPosition + i;
					for (; (till < fragmentEnd && till < position); ++till) {
						const auto ch = fragmentText[till - fragmentPosition];
						if (!ch.isLetterOrNumber() && ch != '_' && ch != '@') {
							break;
						}
					}
					if (till < fragmentEnd
						&& fragmentText[till - fragmentPosition] == ' ') {
						++till;
					}
					cursor.setPosition(till, QTextCursor::KeepAnchor);
					break;
				} else if ((i == position - fragmentPosition
					|| fragmentText[i].isLetter())
					&& fragmentText[i - 1] == '@'
					&& (i > 2)
					&& (fragmentText[i - 2].isLetterOrNumber()
						|| fragmentText[i - 2] == '_')
					&& !mentionInCommand) {
					mentionInCommand = true;
					--i;
					continue;
				}
				break;
			}
			if (position - fragmentPosition - i > 127
				|| (!mentionInCommand
					&& (position - fragmentPosition - i > 63))
				|| (!fragmentText[i - 1].isLetterOrNumber()
					&& fragmentText[i - 1] != '_')) {
				break;
			}
		}
		break;
	}
	if (tagId.isEmpty()) {
		cursor.insertText(text + ' ', _defaultCharFormat);
	} else {
		_insertedTags.clear();
		_insertedTags.push_back({ 0, int(text.size()), tagId });
		_insertedTagsAreFromMime = false;
		cursor.insertText(text + ' ');
		_insertedTags.clear();
	}
}

bool InputField::heightAutoupdated() {
	if (_minHeight < 0
		|| _maxHeight < 0
		|| _inHeightCheck
		|| _mode == Mode::SingleLine) {
		return false;
	}
	_inHeightCheck = true;
	const auto guard = gsl::finally([&] { _inHeightCheck = false; });

	SendPendingMoveResizeEvents(this);

	const auto contentHeight = int(std::ceil(document()->size().height()))
		+ _st.textMargins.top()
		+ _st.textMargins.bottom()
		+ _additionalMargins.top()
		+ _additionalMargins.bottom();
	const auto newHeight = std::clamp(contentHeight, _minHeight, _maxHeight);
	if (height() != newHeight) {
		resize(width(), newHeight);
		return true;
	}
	return false;
}

void InputField::checkContentHeight() {
	if (heightAutoupdated()) {
		_heightChanges.fire({});
	}
}

void InputField::handleTouchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) {
			return;
		}
		_touchTimer.callOnce(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!e->touchPoints().isEmpty()) {
			touchUpdate(e->touchPoints().cbegin()->screenPos().toPoint());
		}
	} break;

	case QEvent::TouchEnd: {
		touchFinish();
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.cancel();
	} break;
	}
}

void InputField::touchUpdate(QPoint globalPosition) {
	if (_touchPress
		&& !_touchMove
		&& ((globalPosition - _touchStart).manhattanLength()
			>= QApplication::startDragDistance())) {
		_touchMove = true;
	}
}

void InputField::touchFinish() {
	if (!_touchPress) {
		return;
	}
	const auto weak = MakeWeak(this);
	if (!_touchMove && window()) {
		QPoint mapped(mapFromGlobal(_touchStart));

		if (_touchRightButton) {
			QContextMenuEvent contextEvent(
				QContextMenuEvent::Mouse,
				mapped,
				_touchStart);
			contextMenuEvent(&contextEvent);
		} else {
			QGuiApplication::inputMethod()->show();
		}
	}
	if (weak) {
		_touchTimer.cancel();
		_touchPress
			= _touchMove
			= _touchRightButton
			= _mousePressedInTouch = false;
	}
}

void InputField::paintSurrounding(
		QPainter &p,
		QRect clip,
		float64 errorDegree,
		float64 focusedDegree) {
	if (_st.borderRadius > 0) {
		paintRoundSurrounding(p, clip, errorDegree, focusedDegree);
	} else {
		paintFlatSurrounding(p, clip, errorDegree, focusedDegree);
	}
}

void InputField::paintRoundSurrounding(
		QPainter &p,
		QRect clip,
		float64 errorDegree,
		float64 focusedDegree) {
	const auto divide = _st.borderDenominator ? _st.borderDenominator : 1;
	const auto border = _st.border / float64(divide);
	const auto borderHalf = border / 2.;
	auto pen = anim::pen(_st.borderFg, _st.borderFgActive, focusedDegree);
	pen.setWidthF(border);
	p.setPen(pen);
	p.setBrush(anim::brush(_st.textBg, _st.textBgActive, focusedDegree));

	PainterHighQualityEnabler hq(p);
	const auto radius = _st.borderRadius - borderHalf;
	p.drawRoundedRect(
		QRectF(0, 0, width(), height()).marginsRemoved(
			QMarginsF(borderHalf, borderHalf, borderHalf, borderHalf)),
		radius,
		radius);
}

void InputField::paintFlatSurrounding(
		QPainter &p,
		QRect clip,
		float64 errorDegree,
		float64 focusedDegree) {
	if (_st.textBg->c.alphaF() > 0.) {
		p.fillRect(clip, _st.textBg);
	}
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg);
	}
	const auto borderShownDegree = _a_borderShown.value(1.);
	const auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = std::clamp(_borderAnimationStart, 0, width());
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width() - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height() - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}
}

void InputField::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto r = rect().intersected(e->rect());
	const auto errorDegree = _a_error.value(_error ? 1. : 0.);
	const auto focusedDegree = _a_focused.value(_focused ? 1. : 0.);
	paintSurrounding(p, r, errorDegree, focusedDegree);

	const auto skip = int(base::SafeRound(_inner->document()->documentMargin()));
	const auto margins = _st.textMargins
		+ _st.placeholderMargins
		+ QMargins(skip, skip + _placeholderCustomFontSkip, skip, 0)
		+ _additionalMargins
		+ _customFontMargins;

	if (_st.placeholderScale > 0. && !_placeholderPath.isEmpty()) {
		auto placeholderShiftDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		p.save();
		p.setClipRect(r);

		auto placeholderTop = anim::interpolate(0, _st.placeholderShift, placeholderShiftDegree);

		QRect r(rect().marginsRemoved(margins));
		r.moveTop(r.top() + placeholderTop);
		if (style::RightToLeft()) r.moveLeft(width() - r.left() - r.width());

		auto placeholderScale = 1. - (1. - _st.placeholderScale) * placeholderShiftDegree;
		auto placeholderFg = anim::color(_st.placeholderFg, _st.placeholderFgActive, focusedDegree);
		placeholderFg = anim::color(placeholderFg, _st.placeholderFgError, errorDegree);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(placeholderFg);
		p.translate(r.topLeft());
		p.scale(placeholderScale, placeholderScale);
		p.drawPath(_placeholderPath);

		p.restore();
	} else if (!_placeholder.isEmpty()) {
		const auto placeholderHiddenDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			const auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			p.setFont(_st.placeholderFont);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));
			if (_st.placeholderAlign == style::al_topleft && _placeholderAfterSymbols > 0) {
				const auto skipWidth = placeholderSkipWidth();
				p.drawText(
					margins.left() + skipWidth,
					margins.top() + _st.placeholderFont->ascent,
					_placeholder);
			} else {
				auto r = rect().marginsRemoved(margins);
				r.moveLeft(r.left() + placeholderLeft);
				if (style::RightToLeft()) r.moveLeft(width() - r.left() - r.width());
				p.drawText(r, _placeholder, _st.placeholderAlign);
			}

			p.restore();
		}
	}
	RpWidget::paintEvent(e);
}

int InputField::placeholderSkipWidth() const {
	if (!_placeholderAfterSymbols) {
		return 0;
	}
	const auto &text = getTextWithTags().text;
	auto result = _st.style.font->width(
		text.mid(0, _placeholderAfterSymbols));
	if (_placeholderAfterSymbols > text.size()) {
		result += _st.style.font->spacew;
	}
	return result;
}

void InputField::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([this] { update(); }, 0., 1., _st.duration);
			} else {
				_a_borderShown.start([this] { update(); }, 0., 1., _st.duration);
			}
		} else {
			_a_borderOpacity.start([this] { update(); }, 1., 0., _st.duration);
		}
	}
}

void InputField::focusInEvent(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason)
		? mapFromGlobal(QCursor::pos()).x()
		: (width() / 2);
	InvokeQueued(this, [=] {
		if (QWidget::hasFocus()) {
			focusInner();
		}
	});
}

void InputField::mousePressEvent(QMouseEvent *e) {
	_borderAnimationStart = e->pos().x();
	InvokeQueued(this, [=] { focusInner(); });
}

void InputField::mousePressEventInner(QMouseEvent *e) {
	if (_touchPress && e->button() == Qt::LeftButton) {
		_mousePressedInTouch = true;
		_touchStart = e->globalPos();
	} else {
		_selectedActionQuoteId = lookupActionQuoteId(e->pos());
		_pressedActionQuoteId = _selectedActionQuoteId;
		updateCursorShape();
	}
	if (_pressedActionQuoteId <= 0) {
		_inner->QTextEdit::mousePressEvent(e);
	}
}

void InputField::editPreLanguage(int quoteId, QStringView tag) {
	Expects(IsTagPre(tag));

	if (!_editLanguageCallback) {
		return;
	}
	const auto apply = crl::guard(this, [=](QString language) {
		auto block = FindBlock(document(), quoteId);
		if (block.isValid()) {
			const auto id = kTagPre + language;
			_insertedTags = { { block.position(), block.length() - 1, id } };
			auto cursor = QTextCursor(document());
			cursor.setPosition(block.position());
			cursor.setBlockFormat(PrepareBlockFormat(_st, id));
			_insertedTags.clear();
		}
	});
	_editLanguageCallback(tag.mid(kTagPre.size()).toString(), apply);
}

void InputField::trippleEnterExitBlock(QTextCursor &cursor) {
	const auto block = cursor.block();
	if (!HasBlockTag(block)) {
		return;
	}
	const auto document = cursor.document();
	const auto position = cursor.position();
	const auto blockFrom = block.position();
	const auto blockTill = blockFrom + block.length();
	if (blockTill - blockFrom <= 3
		|| (position != blockFrom + 3 && position != blockTill - 1)) {
		return;
	} else if (document->characterAt(position - 1) != kSoftLine
		|| document->characterAt(position - 2) != kSoftLine
		|| document->characterAt(position - 3) != kSoftLine) {
		return;
	}
	const auto before = (position == blockFrom + 3);
	cursor.setPosition(position - 3, QTextCursor::KeepAnchor);
	cursor.insertText(
		QString(QChar(kHardLine)),
		before ? cursor.charFormat() : _defaultCharFormat);
	if (before) {
		cursor.setPosition(cursor.position() - 1);
	}
	cursor.setBlockFormat(PrepareBlockFormat(_st));
	if (before) {
		setTextCursor(cursor);
	}
}

void InputField::toggleBlockquoteCollapsed(
		int quoteId,
		QStringView tag,
		TextRange range) {
	if (!_customObject) {
		return;
	}
	const auto collapsed = (tag == kTagBlockquoteCollapsed);
	auto text = getTextWithTagsPart(range.from, range.till);
	for (auto i = text.tags.begin(); i != text.tags.end();) {
		auto without = WithBlockTagRemoved(i->id);
		if (without.isEmpty()) {
			i = text.tags.erase(i);
		} else {
			i->id = std::move(without);
			++i;
		}
	}
	if (!collapsed) {
		_customObject->setCollapsedText(quoteId, std::move(text));
	}
	_insertedTagsDelayClear = true;
	const auto now = collapsed ? kTagBlockquote : kTagBlockquoteCollapsed;
	auto cursor = QTextCursor(document());
	cursor.beginEditBlock();
	cursor.setPosition(range.from);
	cursor.setPosition(range.till, QTextCursor::KeepAnchor);
	cursor.removeSelectedText();
	cursor.setBlockFormat(PrepareBlockFormat(_st, now, quoteId));
	if (collapsed) {
		insertWithTags(
			{ range.from, range.from },
			WrapInQuote(std::move(text), kTagBlockquote));
	} else {
		cursor.insertText(
			kObjectReplacement,
			PrepareCollapsedQuoteFormat(quoteId));
		const auto now = cursor.position();
		cursor.movePosition(QTextCursor::End);
		if (cursor.position() == now) {
			cursor.insertBlock(PrepareBlockFormat(_st), _defaultCharFormat);
		} else {
			cursor.setPosition(now + 1);
		}
	}
	cursor.endEditBlock();

	_insertedTagsDelayClear = false;
	_insertedTags.clear();
	_realInsertPosition = -1;

	setTextCursor(cursor);
}

void InputField::blockActionClicked(int quoteId) {
	auto block = FindBlock(document(), quoteId);
	const auto format = block.blockFormat();
	const auto tag = format.property(kQuoteFormatId).toString();
	const auto blockTag = FindBlockTag(tag);
	if (IsTagPre(blockTag)) {
		editPreLanguage(quoteId, blockTag);
	} else {
		toggleBlockquoteCollapsed(
			quoteId,
			blockTag,
			{ block.position(), block.position() + block.length() - 1 });
	}
}

void InputField::mouseReleaseEventInner(QMouseEvent *e) {
	_selectedActionQuoteId = lookupActionQuoteId(e->pos());
	const auto taken = std::exchange(_pressedActionQuoteId, -1);
	if (taken > 0 && taken == _selectedActionQuoteId) {
		blockActionClicked(taken);
	}
	updateCursorShape();
	if (_mousePressedInTouch) {
		touchFinish();
	} else {
		_inner->QTextEdit::mouseReleaseEvent(e);
	}
}

void InputField::mouseMoveEventInner(QMouseEvent *e) {
	if (_mousePressedInTouch) {
		touchUpdate(e->globalPos());
	}
	_selectedActionQuoteId = lookupActionQuoteId(e->pos());
	updateCursorShape();
	_inner->QTextEdit::mouseMoveEvent(e);
}

int InputField::lookupActionQuoteId(QPoint point) const {
	auto shift = std::optional<QPoint>();

	const auto document = _inner->document();
	const auto layout = document->documentLayout();
	const auto collapsedCutoff = CollapsedQuoteCutoff(_st);
	auto block = document->firstBlock();

	while (block.isValid()) {
		const auto format = block.blockFormat();
		const auto id = format.property(kQuoteFormatId).toString();
		const auto collapsed = (id == kTagBlockquoteCollapsed);
		const auto pre = !collapsed && IsTagPre(id);
		const auto st = pre
			? &_st.style.pre
			: (id == kTagBlockquote || collapsed)
			? &_st.style.blockquote
			: nullptr;
		if (st) {
			if (!shift) {
				shift.emplace(
					-_inner->horizontalScrollBar()->value(),
					-_inner->verticalScrollBar()->value());
			}
			const auto rect = layout->blockBoundingRect(block).toRect();
			const auto added = IsTagPre(id)
				? QMargins(0, 0, 0, st->verticalSkip)
				: QMargins();
			const auto target = ExtendForPaint(
				rect.marginsAdded(added),
				*st
			).translated(*shift);
			if (pre
				&& QRect(
					target.x(),
					target.y(),
					target.width(),
					st->header).contains(point)) {
				return format.property(kQuoteId).toInt();
			} else if (!pre
				&& (collapsed || rect.height() > collapsedCutoff)) {
				const auto right = target.x() + target.width();
				const auto bottom = target.y() + target.height();
				const auto w = st->expand.width() + st->expandPosition.x();
				const auto h = st->expand.height() + st->expandPosition.y();
				if (QRect(right - w, bottom - h, w, h).contains(point)) {
					return format.property(kQuoteId).toInt();
				}
			}
		}
		block = block.next();
	}
	return 0;
}

void InputField::updateCursorShape() {
	const auto check = (_pressedActionQuoteId < 0)
		? _selectedActionQuoteId
		: _pressedActionQuoteId;
	_inner->viewport()->setCursor(check > 0
		? style::cur_pointer
		: style::cur_text);
}

void InputField::leaveEventInner(QEvent *e) {
	_selectedActionQuoteId = 0;
	_inner->viewport()->setCursor(style::cur_text);
	_inner->QTextEdit::leaveEvent(e);
}

void InputField::focusInner() {
	auto borderStart = _borderAnimationStart;
	_inner->setFocus();
	_borderAnimationStart = borderStart;
}

int InputField::borderAnimationStart() const {
	return _borderAnimationStart;
}

void InputField::contextMenuEvent(QContextMenuEvent *e) {
	_inner->contextMenuEvent(e);
}

void InputField::focusInEventInner(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason)
		? mapFromGlobal(QCursor::pos()).x()
		: (width() / 2);
	setFocused(true);
	_inner->QTextEdit::focusInEvent(e);
	_focusedChanges.fire(true);
}

void InputField::focusOutEventInner(QFocusEvent *e) {
	setFocused(false);
	_inner->QTextEdit::focusOutEvent(e);
	_focusedChanges.fire(false);
}

void InputField::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start([this] { update(); }, _focused ? 0. : 1., _focused ? 1. : 0., _st.duration);
		startPlaceholderAnimation();
		startBorderAnimation();
	}
}

QSize InputField::sizeHint() const {
	return geometry().size();
}

QSize InputField::minimumSizeHint() const {
	return geometry().size();
}

bool InputField::hasText() const {
	const auto document = _inner->document();
	const auto from = document->begin();
	const auto till = document->end();

	if (from == till) {
		return false;
	}

	for (auto item = from.begin(); !item.atEnd(); ++item) {
		const auto fragment = item.fragment();
		if (!fragment.isValid()) {
			continue;
		} else if (!fragment.text().isEmpty()) {
			return true;
		}
	}
	return (from.next() != till);
}

QString InputField::getTextPart(
		int start,
		int end,
		TagList &outTagsList,
		bool &outTagsChanged,
		std::vector<MarkdownTag> *outMarkdownTags) const {
	Expects((start == 0 && end < 0) || outMarkdownTags == nullptr);

	if (end >= 0 && end <= start) {
		outTagsChanged = !outTagsList.isEmpty();
		outTagsList.clear();
		return {};
	}

	if (start < 0) {
		start = 0;
	}
	const auto full = (start == 0 && end < 0);

	auto textSpoilers = std::optional<RangeAccumulator>();
	auto emojiSpoilers = std::optional<RangeAccumulator>();
	if (full) {
		textSpoilers.emplace(_spoilerRangesText);
		emojiSpoilers.emplace(_spoilerRangesEmoji);
	}

	auto lastTag = QString();
	TagAccumulator tagAccumulator(outTagsList);
	MarkdownTagAccumulator markdownTagAccumulator(outMarkdownTags);
	const auto newline = outMarkdownTags ? QString(1, '\n') : QString();

	const auto document = _inner->document();
	const auto from = full ? document->begin() : document->findBlock(start);
	auto till = (end < 0) ? document->end() : document->findBlock(end);
	if (till.isValid()) {
		till = till.next();
	}

	auto possibleLength = 0;
	for (auto block = from; block != till; block = block.next()) {
		possibleLength += block.length();
	}
	auto result = QString();
	result.reserve(possibleLength);
	if (!full && end < 0) {
		end = possibleLength;
	}

	for (auto block = from; block != till;) {
		// Only full blocks add block tags.
		const auto blockFormat = full
			? block.blockFormat()
			: (start > block.position()
				|| end + 1 < block.position() + block.length())
			? QTextBlockFormat()
			: block.blockFormat();
		for (auto item = block.begin(); !item.atEnd(); ++item) {
			const auto fragment = item.fragment();
			if (!fragment.isValid()) {
				continue;
			}

			const auto fragmentPosition = full ? 0 : fragment.position();
			const auto fragmentEnd = full
				? 0
				: (fragmentPosition + fragment.length());
			const auto format = fragment.charFormat();
			if (!full) {
				if (fragmentPosition == end) {
					const auto tag = FullTag(format, blockFormat);
					tagAccumulator.feed(tag, result.size());
					break;
				} else if (fragmentPosition > end) {
					break;
				} else if (fragmentEnd <= start) {
					continue;
				}
			}

			const auto emojiText = [&] {
				if (format.isImageFormat()) {
					const auto imageName = format.toImageFormat().name();
					if (const auto emoji = Emoji::FromUrl(imageName)) {
						return emoji->text();
					}
				}
				return format.property(kCustomEmojiText).toString();
			}();
			auto text = [&] {
				const auto result = fragment.text();
				if (!full) {
					if (fragmentPosition < start) {
						return result.mid(start - fragmentPosition, end - start);
					} else if (fragmentEnd > end) {
						return result.mid(0, end - fragmentPosition);
					}
				}
				return result;
			}();

			if (full || !text.isEmpty()) {
				lastTag = FullTag(format, blockFormat);
				tagAccumulator.feed(lastTag, result.size());
				if (textSpoilers && HasSpoilerTag(lastTag)) {
					const auto offset = fragment.position();
					const auto length = fragment.length();
					if (!emojiText.isEmpty()) {
						emojiSpoilers->add(offset, length);
					} else {
						textSpoilers->add(offset, length);
					}
				}
			}

			auto begin = text.data();
			auto ch = begin;
			auto adjustedLength = text.size();
			for (const auto end = begin + text.size(); ch != end; ++ch) {
				if (IsNewline(*ch) && ch->unicode() != '\r') {
					*ch = QLatin1Char('\n');
				} else switch (ch->unicode()) {
				case QChar::ObjectReplacementCharacter: {
					if (ch > begin) {
						result.append(begin, ch - begin);
					}
					const auto tag = blockFormat.property(kQuoteFormatId);
					const auto quote = FindBlockTag(tag.toString());
					if (quote == kTagBlockquoteCollapsed) {
						auto collapsed = _customObject
							? _customObject->collapsedText(
								blockFormat.property(kQuoteId).toInt())
							: TextWithTags();
						adjustedLength += collapsed.text.size() - 1;
						auto from = int(result.size());
						tagAccumulator.feed(kTagBlockquoteCollapsed, from);
						for (const auto &tag : collapsed.tags) {
							tagAccumulator.feed(
								TextUtilities::TagWithAdded(
									tag.id,
									kTagBlockquoteCollapsed),
								from + tag.offset);
							tagAccumulator.feed(
								kTagBlockquoteCollapsed,
								from + tag.offset + tag.length);
						}
						result.append(collapsed.text);
					} else {
						adjustedLength += emojiText.size() - 1;
						if (!emojiText.isEmpty()) {
							result.append(emojiText);
						}
					}
					begin = ch + 1;
				} break;
				}
			}
			if (ch > begin) {
				result.append(begin, ch - begin);
			}

			if (full || !text.isEmpty()) {
				markdownTagAccumulator.feed(text, adjustedLength, lastTag);
			}
		}

		block = block.next();
		if (block != till) {
			tagAccumulator.feed(
				TagWithoutCustomEmoji(
					FullTag(block.charFormat(), QTextBlockFormat())),
				result.size());
			result.append('\n');
			markdownTagAccumulator.feed(newline, 1, lastTag);
		}
	}

	tagAccumulator.feed(QString(), result.size());
	tagAccumulator.finish();
	markdownTagAccumulator.finish();

	outTagsChanged = tagAccumulator.changed();
	return result;
}

bool InputField::isUndoAvailable() const {
	return _undoAvailable;
}

bool InputField::isRedoAvailable() const {
	return _redoAvailable;
}

void InputField::processFormatting(int insertPosition, int insertEnd) {
	// Tilde formatting.
	const auto ratio = style::DevicePixelRatio();
	const auto processTilde = (_st.style.font->f.pixelSize() * ratio == 13)
		&& (_st.style.font->f.family() == qstr("Open Sans"));
	auto isTildeFragment = false;
	auto tildeFixedFont = _st.style.font->semibold()->f;

	// First tag handling (the one we inserted text to).
	bool startTagFound = false;
	bool breakTagOnNotLetter = false;

	auto document = _inner->document();

	// Apply inserted tags.
	const auto insertedTagsProcessor = _insertedTagsAreFromMime
		? (_tagMimeProcessor ? _tagMimeProcessor : DefaultTagMimeProcessor)
		: nullptr;
	const auto breakTagOnNotLetterTill = ProcessInsertedTags(
		_st,
		document,
		insertPosition,
		insertEnd,
		_insertedTags,
		_insertedTagsReplace,
		insertedTagsProcessor);
	using ActionType = FormattingAction::Type;
	while (true) {
		FormattingAction action;

		auto blockTag = QString();
		auto checkedTill = insertPosition;
		auto fromBlock = document->findBlock(insertPosition);
		auto tillBlock = document->findBlock(insertEnd);
		if (tillBlock.isValid()) tillBlock = tillBlock.next();

		for (auto block = fromBlock; block != tillBlock; block = block.next()) {
			const auto blockFormat = block.blockFormat();
			blockTag = FindBlockTag(
				blockFormat.property(kQuoteFormatId).toString()).toString();
			if (blockTag == kTagBlockquoteCollapsed) {
				const auto id = blockFormat.property(kQuoteId).toInt();
				if (!_customObject || !id || block.length() == 1) {
					action.type = ActionType::RemoveBlockquote;
					action.intervalStart = block.position();
					break;
				}
				auto collapsedObjectPosition = FindCollapsedQuoteObject(block);
				if (collapsedObjectPosition < 0) {
					action.type = ActionType::CollapseBlockquote;
					action.quoteId = id;
					action.intervalStart = block.position();
					action.intervalEnd = block.position() + block.length() - 1;
					break;
				} else if (collapsedObjectPosition > block.position()) {
					action.type = ActionType::CutCollapsedBefore;
					action.quoteId = id;
					action.intervalStart = block.position();
					action.intervalEnd = collapsedObjectPosition;
					break;
				} else if (collapsedObjectPosition + 2 < block.position() + block.length()) {
					action.type = ActionType::CutCollapsedAfter;
					action.intervalStart = collapsedObjectPosition + 1;
					break;
				}
				continue;
			} else if (blockFormat.lineHeightType() != QTextBlockFormat::FixedHeight
				&& blockTag != kTagBlockquoteCollapsed) {
				action.intervalStart = block.position();
				action.type = ActionType::FixLineHeight;
				break;
			}
			for (auto fragmentIt = block.begin(); !fragmentIt.atEnd(); ++fragmentIt) {
				auto fragment = fragmentIt.fragment();
				Assert(fragment.isValid());

				const auto fragmentPosition = fragment.position();
				const auto fragmentEnd = fragmentPosition + fragment.length();
				if (insertPosition > fragmentEnd) {
					// In case insertPosition == fragmentEnd we still
					// need to fill startTagFound / breakTagOnNotLetter.
					// This can happen if we inserted a newline after
					// a text fragment with some formatting tag, like Bold.
					continue;
				}
				int changedPositionInFragment = insertPosition - fragmentPosition; // Can be negative.
				int changedEndInFragment = insertEnd - fragmentPosition;
				if (changedEndInFragment < 0) {
					break;
				}

				auto format = fragment.charFormat();
				if (!format.hasProperty(kTagProperty)) {
					action.type = ActionType::RemoveTag;
					action.intervalStart = fragmentPosition;
					action.intervalEnd = fragmentEnd;
					break;
				} else if (IsTagPre(blockTag)
					&& blockTag != format.property(kTagProperty).toString()
					&& format.objectType() != kCustomEmojiFormat) {
					action.type = ActionType::FixPreTag;
					action.intervalStart = fragmentPosition;
					action.intervalEnd = fragmentEnd;
					break;
				}
				if (processTilde) {
					const auto formatFont = format.font();
					if (!tildeFixedFont.styleName().isEmpty()
						&& formatFont.styleName().isEmpty()) {
						tildeFixedFont.setStyleName(QString());
					}
					isTildeFragment = (format.font() == tildeFixedFont);
				}

				auto fragmentText = fragment.text();
				auto *textStart = fragmentText.constData();
				auto *textEnd = textStart + fragmentText.size();

				if (_customObject
					&& format.objectType() == kCustomEmojiFormat) {
					if (fragmentText == kObjectReplacement) {
						checkedTill = fragmentEnd;
						continue;
					}
					action.type = ActionType::InsertCustomEmoji;
					action.intervalStart = fragmentPosition;
					action.intervalEnd = fragmentPosition
						+ fragmentText.size();
					action.customEmojiText = fragmentText;
					action.customEmojiLink = format.property(
						kCustomEmojiLink).toString();
					break;
				} else if (_customObject
					&& format.objectType() == kCollapsedQuoteFormat) {
					action.type = ActionType::MakeCollapsedBlockquote;
					action.quoteId = format.property(kQuoteId).toInt();
					action.intervalStart = fragmentPosition;
					action.intervalEnd = fragmentPosition + fragmentText.size();
					break;
				}

				const auto with = format.property(kInstantReplaceWithId);
				if (with.isValid()) {
					const auto string = with.toString();
					if (fragmentText != string) {
						action.type = ActionType::ClearInstantReplace;
						action.intervalStart = fragmentPosition
							+ (fragmentText.startsWith(string)
								? string.size()
								: 0);
						action.intervalEnd = fragmentPosition
							+ fragmentText.size();
						break;
					}
				}

				if (format.hasProperty(kCustomEmojiLink)
					&& !format.property(kCustomEmojiLink).toString().isEmpty()) {
					action.type = ActionType::RemoveCustomEmoji;
					action.existingTags = format.property(kTagProperty).toString();
					action.intervalStart = fragmentPosition;
					action.intervalEnd = fragmentPosition
						+ fragmentText.size();
					break;
				}
				if (!startTagFound) {
					startTagFound = true;
					auto tagName = format.property(kTagProperty).toString();
					if (!tagName.isEmpty()) {
						breakTagOnNotLetter = WasInsertTillTheEndOfTag(
							block,
							fragmentIt,
							insertEnd);
					}
				}

				auto *ch = textStart + qMax(changedPositionInFragment, 0);
				for (; ch < textEnd; ++ch) {
					const auto removeNewline = (_mode != Mode::MultiLine)
						&& IsNewline(*ch);
					if (removeNewline) {
						if (action.type == ActionType::Invalid) {
							action.type = ActionType::RemoveNewline;
							action.intervalStart = fragmentPosition + (ch - textStart);
							action.intervalEnd = action.intervalStart + 1;
						}
						break;
					}

					auto emojiLength = 0;
					if (const auto emoji = Emoji::Find(ch, textEnd, &emojiLength)) {
						// Replace emoji if no current action is prepared.
						if (action.type == ActionType::Invalid) {
							action.type = ActionType::InsertEmoji;
							action.emoji = emoji;
							action.intervalStart = fragmentPosition + (ch - textStart);
							action.intervalEnd = action.intervalStart + emojiLength;
						}
						if (emojiLength > 1) {
							_emojiSurrogateAmount += emojiLength - 1;
						}
						break;
					}

					if (breakTagOnNotLetter && !ch->isLetterOrNumber()) {
						// Remove tag name till the end if no current action is prepared.
						if (action.type != ActionType::Invalid) {
							break;
						}
						breakTagOnNotLetter = false;
						if (fragmentPosition + (ch - textStart) < breakTagOnNotLetterTill) {
							action.type = ActionType::RemoveTag;
							action.intervalStart = fragmentPosition + (ch - textStart);
							action.intervalEnd = breakTagOnNotLetterTill;
							break;
						}
					}
					if (processTilde) { // Tilde symbol fix in OpenSans.
						bool tilde = (ch->unicode() == '~');
						if ((tilde && !isTildeFragment) || (!tilde && isTildeFragment)) {
							if (action.type == ActionType::Invalid) {
								action.type = ActionType::TildeFont;
								action.intervalStart = fragmentPosition + (ch - textStart);
								action.intervalEnd = action.intervalStart + 1;
								action.tildeTag = format.property(kTagProperty).toString();
								action.isTilde = tilde;
							} else {
								++action.intervalEnd;
							}
						} else if (action.type == ActionType::TildeFont) {
							break;
						}
					}

					if (ch + 1 < textEnd && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) {
						++ch;
					}
				}
				if (action.type != ActionType::Invalid) {
					break;
				}
				checkedTill = fragmentEnd;
			}
			if (action.type != ActionType::Invalid) {
				break;
			} else if (_mode != Mode::MultiLine
				&& block.next() != document->end()) {
				action.type = ActionType::RemoveNewline;
				action.intervalStart = block.next().position() - 1;
				action.intervalEnd = action.intervalStart + 1;
				break;
			} else if (breakTagOnNotLetter) {
				// In case we need to break on not letter and we didn't
				// find any non letter symbol, we found it here - a newline.
				breakTagOnNotLetter = false;
				if (checkedTill < breakTagOnNotLetterTill) {
					action.type = ActionType::RemoveTag;
					action.intervalStart = checkedTill;
					action.intervalEnd = breakTagOnNotLetterTill;
					break;
				}
			}
		}
		if (action.type != ActionType::Invalid) {
			PrepareFormattingOptimization(document);

			if (action.type == ActionType::CollapseBlockquote) {
				toggleBlockquoteCollapsed(
					action.quoteId,
					kTagBlockquote,
					{ action.intervalStart, action.intervalEnd });
				continue;
			}

			auto cursor = QTextCursor(document);
			if (action.type == ActionType::CutCollapsedBefore) {
				auto realCursor = textCursor();
				const auto wasAtEdge = !realCursor.hasSelection()
					&& realCursor.position() == action.intervalEnd;
				cursor.setPosition(action.intervalEnd);
				if (action.intervalEnd > 0
					&& IsNewline(
						document->characterAt(action.intervalEnd - 1))) {
					cursor.setPosition(
						action.intervalEnd - 1,
						QTextCursor::KeepAnchor);
					cursor.insertText(
						QString(QChar(kHardLine)),
						_defaultCharFormat);
				} else {
					cursor.insertBlock(
						PrepareBlockFormat(
							_st,
							kTagBlockquoteCollapsed,
							action.quoteId),
						_defaultCharFormat);
				}
				cursor.setPosition(action.intervalStart);
				cursor.setBlockFormat(PrepareBlockFormat(_st));
				if (wasAtEdge) {
					realCursor.setPosition(action.intervalEnd);
					_formattingCursorUpdate = realCursor;
				}
				continue;
			}
			cursor.setPosition(action.intervalStart);
			if (action.type == ActionType::FixLineHeight) {
				cursor.setBlockFormat(PrepareBlockFormat(_st));
				continue;
			} else if (action.type == ActionType::CutCollapsedAfter) {
				if (IsNewline(document->characterAt(action.intervalStart))) {
					cursor.setPosition(
						action.intervalStart + 1,
						QTextCursor::KeepAnchor);
					cursor.insertText(
						QString(QChar(kHardLine)),
						_defaultCharFormat);
				} else {
					cursor.insertBlock(
						PrepareBlockFormat(_st),
						_defaultCharFormat);
					++insertEnd;
				}
				continue;
			} else if (action.type == ActionType::RemoveBlockquote) {
				cursor.setBlockFormat(PrepareBlockFormat(_st));
				continue;
			} else if (action.type == ActionType::MakeCollapsedBlockquote) {
				auto text = _customObject->collapsedText(action.quoteId);
				if (text.text.isEmpty()) {
					cursor.setPosition(action.intervalEnd);
					cursor.removeSelectedText();
				} else {
					const auto blockFormat = PrepareBlockFormat(
						_st,
						kTagBlockquoteCollapsed);
					const auto id = blockFormat.property(kQuoteId).toInt();
					_customObject->setCollapsedText(id, std::move(text));

					const auto format = PrepareCollapsedQuoteFormat(id);
					auto bBlock = document->findBlock(action.intervalStart);
					if (bBlock.position() != action.intervalStart) {
						cursor.insertText(QString(QChar(kHardLine)), format);
						++action.intervalStart;
						++action.intervalEnd;
					}
					cursor.setBlockFormat(blockFormat);
					cursor.setBlockCharFormat(format);
					const auto after = action.intervalEnd + 1;
					auto eBlock = document->findBlock(after);
					if (eBlock.position() != after) {
						cursor.setPosition(action.intervalEnd);
						cursor.insertBlock(
							PrepareBlockFormat(_st),
							_defaultCharFormat);
					}
					cursor.setPosition(action.intervalStart);
					cursor.setPosition(
						action.intervalEnd,
						QTextCursor::KeepAnchor);
					cursor.setCharFormat(format);
				}
			}
			cursor.setPosition(action.intervalEnd, QTextCursor::KeepAnchor);
			if (action.type == ActionType::InsertEmoji
				|| action.type == ActionType::InsertCustomEmoji) {
				if (action.type == ActionType::InsertEmoji) {
					InsertEmojiAtCursor(cursor, action.emoji);
				} else {
					InsertCustomEmojiAtCursor(
						this,
						cursor,
						action.customEmojiText,
						action.customEmojiLink);
				}
				insertPosition = action.intervalStart + 1;
				if (insertEnd >= action.intervalEnd) {
					insertEnd -= action.intervalEnd
						- action.intervalStart
						- 1;
				}
			} else if (action.type == ActionType::RemoveTag) {
				RemoveDocumentTags(
					_st,
					document,
					action.intervalStart,
					action.intervalEnd);
			} else if (action.type == ActionType::FixPreTag) {
				cursor.setCharFormat(PrepareTagFormat(_st, blockTag));
			} else if (action.type == ActionType::RemoveCustomEmoji) {
				RemoveCustomEmojiTag(
					_st,
					document,
					action.existingTags,
					action.intervalStart,
					action.intervalEnd);
			} else if (action.type == ActionType::TildeFont) {
				auto format = QTextCharFormat();
				format.setFont(action.isTilde
					? tildeFixedFont
					: PrepareTagFormat(_st, action.tildeTag).font());
				cursor.mergeCharFormat(format);
				insertPosition = action.intervalEnd;
			} else if (action.type == ActionType::ClearInstantReplace) {
				auto format = _defaultCharFormat;
				ApplyTagFormat(format, cursor.charFormat());
				cursor.setCharFormat(format);
			} else if (action.type == ActionType::RemoveNewline) {
				cursor.insertText(u" "_q);
				insertPosition = action.intervalStart;
			}
		} else {
			break;
		}
	}
}

void InputField::forceProcessContentsChanges() {
	PostponeCall(this, [=] {
		handleContentsChanged();
	});
}

void InputField::documentContentsChanged(
		int position,
		int charsRemoved,
		int charsAdded) {
	if (_correcting) {
		return;
	}
	// In case of input method events Qt emits
	// document content change signals for a whole
	// text block where the even took place.
	// This breaks our wysiwyg markup, so we adjust
	// the parameters to match the real change.
	if (_inputMethodCommit.has_value()
		&& charsAdded > _inputMethodCommit->size()
		&& charsRemoved > 0) {
		const auto inBlockBefore = charsAdded - _inputMethodCommit->size();
		if (charsRemoved >= inBlockBefore) {
			charsAdded -= inBlockBefore;
			charsRemoved -= inBlockBefore;
			position += inBlockBefore;
		}
	}

	const auto document = _inner->document();

	// Qt bug workaround https://bugreports.qt.io/browse/QTBUG-49062
	if (!position) {
		auto cursor = QTextCursor(document);
		cursor.movePosition(QTextCursor::End);
		if (position + charsAdded > cursor.position()) {
			const auto delta = position + charsAdded - cursor.position();
			if (charsRemoved >= delta) {
				charsAdded -= delta;
				charsRemoved -= delta;
			}
		}
	}

	const auto insertPosition = (_realInsertPosition >= 0)
		? _realInsertPosition
		: position;
	const auto insertLength = (_realInsertPosition >= 0)
		? _realCharsAdded
		: charsAdded;

	_correcting = true;
	QTextCursor(document).joinPreviousEditBlock();

	chopByMaxLength(insertPosition, insertLength);
	if (document->availableRedoSteps() == 0) {
		const auto pageSize = document->pageSize();
		processFormatting(insertPosition, insertPosition + insertLength);
		if (document->pageSize() != pageSize) {
			document->setPageSize(pageSize);
		}
	}
	if (document->isEmpty()) {
		textCursor().setBlockFormat(PrepareBlockFormat(_st));
	}
	updateRootFrameFormat();
	_correcting = false;
	QTextCursor(document).endEditBlock();

	if (_formattingCursorUpdate) {
		setTextCursor(*base::take(_formattingCursorUpdate));
		ensureCursorVisible();
	}

	handleContentsChanged();
	const auto added = charsAdded - _emojiSurrogateAmount;
	_documentContentsChanges.fire({ position, charsRemoved, added });
	_emojiSurrogateAmount = 0;
}

void InputField::updateRootFrameFormat() {
	const auto document = _inner->document();
	auto format = document->rootFrame()->frameFormat();
	const auto propertyId = QTextFrameFormat::FrameTopMargin;
	const auto topMargin = format.property(propertyId).toInt();
	const auto wantedTopMargin = StartsWithPre(document)
		? (_st.style.pre.padding.top()
			+ _st.style.pre.header
			+ _st.style.pre.verticalSkip)
		: _requestedDocumentTopMargin;
	if (_settingDocumentMargin) {
		_requestedDocumentTopMargin = topMargin;
	} else if (topMargin != wantedTopMargin) {
		const auto value = QVariant::fromValue(1. * wantedTopMargin);
		format.setProperty(propertyId, value);
		document->rootFrame()->setFrameFormat(format);
	}
}

void InputField::chopByMaxLength(int insertPosition, int insertLength) {
	Expects(_correcting);

	if (_maxLength < 0) {
		return;
	}

	auto cursor = QTextCursor(document());
	cursor.movePosition(QTextCursor::End);
	const auto fullSize = cursor.position();
	const auto toRemove = fullSize - _maxLength;
	if (toRemove > 0) {
		if (toRemove > insertLength) {
			if (insertLength) {
				cursor.setPosition(insertPosition);
				cursor.setPosition(
					(insertPosition + insertLength),
					QTextCursor::KeepAnchor);
				cursor.removeSelectedText();
			}
			cursor.setPosition(fullSize - (toRemove - insertLength));
			cursor.setPosition(fullSize, QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
		} else {
			cursor.setPosition(
				insertPosition + (insertLength - toRemove));
			cursor.setPosition(
				insertPosition + insertLength,
				QTextCursor::KeepAnchor);
			cursor.removeSelectedText();
		}
	}
}

void InputField::handleContentsChanged() {
	setErrorShown(false);

	auto tagsChanged = false;
	const auto currentText = getTextPart(
		0,
		-1,
		_lastTextWithTags.tags,
		tagsChanged,
		_markdownEnabledState.disabled() ? nullptr : &_lastMarkdownTags);

	//highlightMarkdown();
	if (_spoilerRangesText.empty() && _spoilerRangesEmoji.empty()) {
		_spoilerOverlay = nullptr;
	} else if (_customObject) {
		if (!_spoilerOverlay) {
			_spoilerOverlay = _customObject->createSpoilerOverlay();
			_spoilerOverlay->setGeometry(_inner->rect());
		}
		const auto cursor = textCursor();
		_customObject->refreshSpoilerShown({
			cursor.selectionStart(),
			cursor.selectionEnd(),
		});
	}

	if (tagsChanged || (_lastTextWithTags.text != currentText)) {
		_lastTextWithTags.text = currentText;
		const auto weak = MakeWeak(this);
		_changes.fire({});
		if (!weak) {
			return;
		}
		checkContentHeight();
	}
	startPlaceholderAnimation();
	if (_lastTextWithTags.text.isEmpty()) {
		if (const auto object = _customObject.get()) {
			object->clearEmoji();
		}
	}
	Integration::Instance().textActionsUpdated();
}

void InputField::highlightMarkdown() {
	// Highlighting may interfere with markdown parsing -> inaccurate.
	// For debug.
	auto from = 0;
	auto applyColor = [&](int a, int b, QColor color) {
		auto cursor = textCursor();
		cursor.setPosition(a);
		cursor.setPosition(b, QTextCursor::KeepAnchor);
		auto format = QTextCharFormat();
		format.setForeground(color);
		cursor.mergeCharFormat(format);
		from = b;
	};
	for (const auto &tag : _lastMarkdownTags) {
		if (tag.internalStart > from) {
			applyColor(from, tag.internalStart, QColor(0, 0, 0));
		} else if (tag.internalStart < from) {
			continue;
		}
		applyColor(
			tag.internalStart,
			tag.internalStart + tag.internalLength,
			(tag.closed
				? QColor(0, 128, 0)
				: QColor(128, 0, 0)));
	}
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	if (const auto till = cursor.position(); till > from) {
		applyColor(from, till, QColor(0, 0, 0));
	}
}

void InputField::setDisplayFocused(bool focused) {
	setFocused(focused);
	finishAnimating();
}

void InputField::selectAll() {
	auto cursor = _inner->textCursor();
	cursor.setPosition(0);
	cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	_inner->setTextCursor(cursor);
}

void InputField::finishAnimating() {
	_a_focused.stop();
	_a_error.stop();
	_a_placeholderShifted.stop();
	_a_borderShown.stop();
	_a_borderOpacity.stop();
	update();
}

void InputField::setPlaceholderHidden(bool forcePlaceholderHidden) {
	_forcePlaceholderHidden = forcePlaceholderHidden;
	startPlaceholderAnimation();
}

void InputField::startPlaceholderAnimation() {
	const auto textLength = [&] {
		return getTextWithTags().text.size() + _lastPreEditText.size();
	};
	const auto placeholderShifted = _forcePlaceholderHidden
		|| (_focused && _st.placeholderScale > 0.)
		|| (textLength() > _placeholderAfterSymbols);
	if (_placeholderShifted != placeholderShifted) {
		_placeholderShifted = placeholderShifted;
		_a_placeholderShifted.start(
			[=] { update(); },
			_placeholderShifted ? 0. : 1.,
			_placeholderShifted ? 1. : 0.,
			_st.duration);
	}
}

QMimeData *InputField::createMimeDataFromSelectionInner() const {
	const auto cursor = _inner->textCursor();
	const auto start = cursor.selectionStart();
	const auto end = cursor.selectionEnd();
	return TextUtilities::MimeDataFromText((end > start)
		? getTextWithTagsPart(start, end)
		: TextWithTags()
	).release();
}

void InputField::customUpDown(bool isCustom) {
	_customUpDown = isCustom;
}

void InputField::customTab(bool isCustom) {
	_customTab = isCustom;
}

void InputField::setSubmitSettings(SubmitSettings settings) {
	_submitSettings = settings;
}

not_null<QTextDocument*> InputField::document() {
	return _inner->document();
}

not_null<const QTextDocument*> InputField::document() const {
	return _inner->document();
}

void InputField::setTextCursor(const QTextCursor &cursor) {
	return _inner->setTextCursor(cursor);
}

QTextCursor InputField::textCursor() const {
	return _inner->textCursor();
}

void InputField::setCursorPosition(int pos) {
	auto cursor = _inner->textCursor();
	cursor.setPosition(pos);
	_inner->setTextCursor(cursor);
}

void InputField::setText(const QString &text) {
	setTextWithTags({ text, {} });
}

void InputField::setTextWithTags(
		const TextWithTags &textWithTags,
		HistoryAction historyAction) {
	auto prepared = PrepareForInsert(textWithTags);
	_insertedTags = prepared.tags;
	_insertedTagsAreFromMime = false;
	_realInsertPosition = 0;
	_realCharsAdded = prepared.text.size();
	const auto document = _inner->document();
	auto cursor = QTextCursor(document);
	if (historyAction == HistoryAction::Clear) {
		document->setUndoRedoEnabled(false);
		if (const auto object = _customObject.get()) {
			object->clearEmoji();
			object->clearQuotes();
		}
		cursor.beginEditBlock();
	} else if (historyAction == HistoryAction::MergeEntry) {
		cursor.joinPreviousEditBlock();
	} else {
		cursor.beginEditBlock();
	}
	cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	cursor.insertText(prepared.text, _defaultCharFormat);
	cursor.movePosition(QTextCursor::End);
	cursor.endEditBlock();
	if (historyAction == HistoryAction::Clear) {
		document->setUndoRedoEnabled(true);
	}
	_insertedTags.clear();
	_realInsertPosition = -1;
	finishAnimating();
}

TextWithTags InputField::getTextWithTagsPart(int start, int end) const {
	auto changed = false;
	auto result = TextWithTags();
	result.text = getTextPart(start, end, result.tags, changed);
	return result;
}

TextWithTags InputField::getTextWithAppliedMarkdown() const {
	if (_markdownEnabledState.disabled() || _lastMarkdownTags.empty()) {
		return getTextWithTags();
	}
	const auto &originalText = _lastTextWithTags.text;
	const auto &originalTags = _lastTextWithTags.tags;

	// Ignore tags that partially intersect some http-links.
	// This will allow sending http://test.com/__test__/test correctly.
	const auto links = TextUtilities::ParseEntities(
		originalText,
		0).entities;

	auto result = TextWithTags();
	result.text.reserve(originalText.size());
	result.tags.reserve(originalTags.size() + _lastMarkdownTags.size());
	auto removed = 0;
	auto originalTag = originalTags.begin();
	const auto originalTagsEnd = originalTags.end();
	const auto addOriginalTagsUpTill = [&](int offset) {
		while (originalTag != originalTagsEnd
			&& originalTag->offset + originalTag->length <= offset) {
			result.tags.push_back(*originalTag++);
			result.tags.back().offset -= removed;
		}
	};
	auto from = 0;
	const auto addOriginalTextUpTill = [&](int offset) {
		if (offset > from) {
			result.text.append(base::StringViewMid(originalText, from, offset - from));
		}
	};
	auto link = links.begin();
	const auto linksEnd = links.end();
	for (const auto &tag : _lastMarkdownTags) {
		const auto tagLength = int(tag.tag.size());
		if (!tag.closed || tag.adjustedStart < from) {
			continue;
		}
		auto entityLength = tag.adjustedLength - 2 * tagLength;
		if (entityLength <= 0) {
			continue;
		}
		addOriginalTagsUpTill(tag.adjustedStart);
		const auto tagAdjustedEnd = tag.adjustedStart + tag.adjustedLength;
		if (originalTag != originalTagsEnd
			&& originalTag->offset < tagAdjustedEnd) {
			continue;
		}
		while (link != linksEnd
			&& link->offset() + link->length() <= tag.adjustedStart) {
			++link;
		}
		if (link != linksEnd
			&& link->offset() < tagAdjustedEnd
			&& (link->offset() + link->length() > tagAdjustedEnd
				|| link->offset() < tag.adjustedStart)) {
			continue;
		}
		addOriginalTextUpTill(tag.adjustedStart);

		auto tagId = tag.tag;
		auto entityStart = tag.adjustedStart + tagLength;
		if (tagId == kTagPre) {
			// Remove redundant newlines for pre.
			// If ``` is on a separate line add only one newline.
			const auto languageName = ReadPreLanguageName(
				originalText,
				entityStart,
				entityLength);
			if (!languageName.isEmpty()) {
				// ```language-name{\n}code
				entityStart += languageName.size() + 1;
				entityLength -= languageName.size() + 1;
				tagId += languageName;
			} else if (IsNewline(originalText[entityStart])
				&& (result.text.isEmpty()
					|| IsNewline(result.text[result.text.size() - 1]))) {
				++entityStart;
				--entityLength;
			}
			const auto entityEnd = entityStart + entityLength;
			if (IsNewline(originalText[entityEnd - 1])
				&& (originalText.size() <= entityEnd + tagLength
					|| IsNewline(originalText[entityEnd + tagLength]))) {
				--entityLength;
			}
		}

		if (entityLength > 0) {
			// Add tag text and entity.
			result.tags.push_back(TextWithTags::Tag{
				int(result.text.size()),
				entityLength,
				tagId });
			result.text.append(base::StringViewMid(
				originalText,
				entityStart,
				entityLength));
		}

		from = tag.adjustedStart + tag.adjustedLength;
		removed += (tag.adjustedLength - entityLength);
	}
	addOriginalTagsUpTill(originalText.size());
	addOriginalTextUpTill(originalText.size());
	return result;
}

void InputField::clear() {
	_inner->clear();
	startPlaceholderAnimation();
	if (const auto object = _customObject.get()) {
		object->clearEmoji();
	}
}

bool InputField::hasFocus() const {
	return _inner->hasFocus();
}

void InputField::setFocus() {
	_inner->setFocus();
}

void InputField::clearFocus() {
	_inner->clearFocus();
}

void InputField::ensureCursorVisible() {
	_inner->ensureCursorVisible();
}

not_null<QTextEdit*> InputField::rawTextEdit() {
	return _inner.get();
}

not_null<const QTextEdit*> InputField::rawTextEdit() const {
	return _inner.get();
}

bool InputField::ShouldSubmit(
		SubmitSettings settings,
		Qt::KeyboardModifiers modifiers) {
	const auto shift = modifiers.testFlag(Qt::ShiftModifier);
	const auto ctrl = modifiers.testFlag(Qt::ControlModifier)
		|| modifiers.testFlag(Qt::MetaModifier);
	return (ctrl && shift)
		|| (ctrl
			&& settings != SubmitSettings::None
			&& settings != SubmitSettings::Enter)
		|| (!ctrl
			&& !shift
			&& settings != SubmitSettings::None
			&& settings != SubmitSettings::CtrlEnter);
}

void InputField::keyPressEventInner(QKeyEvent *e) {
	const auto shift = e->modifiers().testFlag(Qt::ShiftModifier);
	const auto alt = e->modifiers().testFlag(Qt::AltModifier);
	const auto macmeta = Platform::IsMac()
		&& e->modifiers().testFlag(Qt::ControlModifier)
		&& !e->modifiers().testFlag(Qt::MetaModifier)
		&& !e->modifiers().testFlag(Qt::AltModifier);
	const auto ctrl = e->modifiers().testFlag(Qt::ControlModifier)
		|| e->modifiers().testFlag(Qt::MetaModifier);
	const auto enterSubmit = (_mode != Mode::MultiLine)
		|| ShouldSubmit(_submitSettings, e->modifiers());
	const auto enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);
	const auto backspace = (e->key() == Qt::Key_Backspace);
	if (e->key() == Qt::Key_Left
		|| e->key() == Qt::Key_Right
		|| e->key() == Qt::Key_Up
		|| e->key() == Qt::Key_Down
		|| e->key() == Qt::Key_Home
		|| e->key() == Qt::Key_End) {
		_reverseMarkdownReplacement = false;
	}

	if (backspace && macmeta) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (backspace
		&& e->modifiers() == 0
		&& revertFormatReplace()) {
		e->accept();
	} else if (backspace && jumpOutOfBlockByBackspace()) {
		e->accept();
	} else if (enter && enterSubmit) {
		_submits.fire(e->modifiers());
	} else if (e->key() == Qt::Key_Escape) {
		e->ignore();
		_cancelled.fire({});
	} else if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
		if (alt || ctrl) {
			e->ignore();
		} else if (_customTab) {
			_tabbed.fire({});
		} else if (!focusNextPrevChild(e->key() == Qt::Key_Tab && !shift)) {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Search || e == QKeySequence::Find) {
		e->ignore();
	} else if (handleMarkdownKey(e)) {
		e->accept();
	} else if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
		e->ignore();
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		const auto cursor = textCursor();
		const auto start = cursor.selectionStart();
		const auto end = cursor.selectionEnd();
		if (end > start) {
			QGuiApplication::clipboard()->setText(
				getTextWithTagsPart(start, end).text,
				QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	} else {
		const auto text = e->text();
		auto cursor = textCursor();
		const auto oldPosition = cursor.position();
		const auto oldSelection = cursor.hasSelection();
		const auto oldModifiers = e->modifiers();
		const auto allowedModifiers = (enter && ctrl)
			? (~Qt::ControlModifier)
			: (enter && shift)
			? (~Qt::ShiftModifier)
			: oldModifiers;
		const auto changeModifiers = (oldModifiers & ~allowedModifiers) != 0;
		if (changeModifiers) {
			e->setModifiers(oldModifiers & allowedModifiers);
		}

		// If we enable this, the Undo/Redo will work through Key_Space
		// insertions, because they will be in edit blocks with the following
		// text char format changes. But this will make every entered letter
		// have a separate Undo block without grouping input together.
		//
		//const auto createEditBlock = (e != QKeySequence::Undo)
		//	&& (e != QKeySequence::Redo);
		const auto createEditBlock = enter
			|| backspace
			|| (e->key() == Qt::Key_Space)
			|| (e->key() == Qt::Key_Delete);
		if (createEditBlock) {
			cursor.beginEditBlock();
		}
		if (e == QKeySequence::InsertParagraphSeparator) {
			// qtbase commit dbb9579566f3accd8aa5fe61db9692991117afd3 introduced
			// special logic for repeated 'Enter' key presses, which drops the
			// block format instead of inserting a newline in case the block format
			// is non-trivial. For custom fonts we use non-trivial block formats
			// always for the entire QTextEdit, so we revert that logic and simply
			// insert a newline as it was before Qt 6.X.Y where this was added.

			// Also we insert a kSoftLine instead of a block, because we want
			// newlines to belong to the same block by default (blockquotes).
			if (!cursor.hasSelection() && !HasBlockTag(cursor.block())) {
				cursor.insertText(QString(QChar(kHardLine)));
			} else {
				cursor.insertText(QString(QChar(kSoftLine)));
				trippleEnterExitBlock(cursor);
			}
			e->accept();
		} else {
			_inner->QTextEdit::keyPressEvent(e);
		}
		if (createEditBlock) {
			cursor.endEditBlock();
		}
		_inner->ensureCursorVisible();
		if (changeModifiers) {
			e->setModifiers(oldModifiers);
		}
		auto updatedCursor = textCursor();
		if (updatedCursor.position() == oldPosition) {
			const auto shift = e->modifiers().testFlag(Qt::ShiftModifier);
			bool check = false;
			if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_Up) {
				updatedCursor.movePosition(QTextCursor::Start, shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_Down) {
				updatedCursor.movePosition(QTextCursor::End, shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (!oldSelection
				&& (e->key() == Qt::Key_Left
					|| e->key() == Qt::Key_Right
					|| e->key() == Qt::Key_Backspace)) {
				e->ignore();
			}
			if (check) {
				if (oldPosition == updatedCursor.position()) {
					if (shift || !exitQuoteWithNewBlock(e->key())) {
						e->ignore();
					}
				} else {
					setTextCursor(updatedCursor);
				}
			}
		}
		if (!processMarkdownReplaces(text)) {
			processInstantReplaces(text);
		}
	}
}

bool InputField::exitQuoteWithNewBlock(int key) {
	const auto up = (key == Qt::Key_Up);
	if (!up && key != Qt::Key_Down) {
		return false;
	}
	auto cursor = textCursor();
	if (cursor.hasSelection()
		|| !cursor.blockFormat().hasProperty(kQuoteFormatId)) {
		return false;
	}
	if (up) {
		cursor.beginEditBlock();
		cursor.insertText(QString(QChar(kHardLine)), _defaultCharFormat);
		cursor.movePosition(QTextCursor::Start);
		cursor.setBlockFormat(PrepareBlockFormat(_st));
		cursor.setCharFormat(_defaultCharFormat);
		cursor.endEditBlock();
		setTextCursor(cursor);
		checkContentHeight();
	} else {
		cursor.insertBlock(PrepareBlockFormat(_st), _defaultCharFormat);
	}
	_inner->ensureCursorVisible();
	return true;
}

TextWithTags InputField::getTextWithTagsSelected() const {
	const auto cursor = textCursor();
	const auto start = cursor.selectionStart();
	const auto end = cursor.selectionEnd();
	return (end > start) ? getTextWithTagsPart(start, end) : TextWithTags();
}

bool InputField::handleMarkdownKey(QKeyEvent *e) {
	if (_markdownEnabledState.disabled()) {
		return false;
	}
	const auto matches = [&](const QKeySequence &sequence) {
		const auto searchKey = (e->modifiers() | e->key())
			& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
		const auto events = QKeySequence(searchKey);
		return sequence.matches(events) == QKeySequence::ExactMatch;
	};
	for (const auto &action : MarkdownActions()) {
		if (matches(action.sequence)) {
			return executeMarkdownAction(action);
		}
	}
	return false;
}

auto InputField::selectionEditLinkData(EditLinkSelection selection) const
-> EditLinkData {
	Expects(_editLinkCallback != nullptr);

	const auto position = (selection.from == selection.till
		&& selection.from > 0)
		? (selection.from - 1)
		: selection.from;
	const auto link = [&] {
		return (position != selection.till)
			? CheckFullTextTag(
				getTextWithTagsPart(position, selection.till),
				kTagCheckLinkMeta)
			: QString();
	}();
	const auto simple = EditLinkData{
		selection.from,
		selection.till,
		QString()
	};
	if (!_editLinkCallback(selection, {}, link, EditLinkAction::Check)) {
		return simple;
	}
	Assert(!link.isEmpty());

	struct State {
		QTextBlock block;
		QTextBlock::iterator i;
	};
	const auto document = _inner->document();
	const auto skipInvalid = [&](State &state) {
		if (state.block == document->end()) {
			return false;
		}
		while (state.i.atEnd()) {
			state.block = state.block.next();
			if (state.block == document->end()) {
				return false;
			}
			state.i = state.block.begin();
		}
		return true;
	};
	const auto moveToNext = [&](State &state) {
		Expects(state.block != document->end());
		Expects(!state.i.atEnd());

		++state.i;
	};
	const auto moveToPrevious = [&](State &state) {
		Expects(state.block != document->end());
		Expects(!state.i.atEnd());

		while (state.i == state.block.begin()) {
			if (state.block == document->begin()) {
				state.block = document->end();
				return false;
			}
			state.block = state.block.previous();
			state.i = state.block.end();
		}
		--state.i;
		return true;
	};
	const auto stateTag = [&](const State &state) {
		const auto format = state.i.fragment().charFormat();
		return format.property(kTagProperty).toString();
	};
	const auto stateTagHasLink = [&](const State &state) {
		const auto tag = stateTag(state);
		return (tag == link)
			|| TextUtilities::SplitTags(tag).contains(QStringView(link));
	};
	const auto stateStart = [&](const State &state) {
		return state.i.fragment().position();
	};
	const auto stateEnd = [&](const State &state) {
		const auto fragment = state.i.fragment();
		return fragment.position() + fragment.length();
	};
	auto state = State{ document->findBlock(position) };
	if (state.block != document->end()) {
		state.i = state.block.begin();
	}
	for (; skipInvalid(state); moveToNext(state)) {
		const auto fragmentStart = stateStart(state);
		const auto fragmentEnd = stateEnd(state);
		if (fragmentEnd <= position) {
			continue;
		} else if (fragmentStart >= selection.till) {
			break;
		}
		if (stateTagHasLink(state)) {
			auto start = fragmentStart;
			auto finish = fragmentEnd;
			auto copy = state;
			while (moveToPrevious(copy) && stateTagHasLink(copy)) {
				start = stateStart(copy);
			}
			while (skipInvalid(state) && stateTagHasLink(state)) {
				finish = stateEnd(state);
				moveToNext(state);
			}
			return { start, finish, link };
		}
	}
	return simple;
}

auto InputField::editLinkSelection(QContextMenuEvent *e) const
-> EditLinkSelection {
	const auto cursor = textCursor();
	if (!cursor.hasSelection() && e->reason() == QContextMenuEvent::Mouse) {
		const auto clickCursor = _inner->cursorForPosition(
			_inner->viewport()->mapFromGlobal(e->globalPos()));
		if (!clickCursor.isNull() && !clickCursor.hasSelection()) {
			return {
				clickCursor.position(),
				clickCursor.position()
			};
		}
	}
	return {
		cursor.selectionStart(),
		cursor.selectionEnd()
	};
}

void InputField::editMarkdownLink(EditLinkSelection selection) {
	if (!_editLinkCallback) {
		return;
	}
	const auto data = selectionEditLinkData(selection);
	auto text = getTextWithTagsPart(data.from, data.till);
	for (auto i = text.tags.begin(); i != text.tags.end();) {
		auto all = TextUtilities::SplitTags(i->id);
		auto j = all.begin();
		for (auto j = all.begin(); j != all.end();) {
			if (IsValidMarkdownLink(*j)) {
				j = all.erase(j);
			} else {
				++j;
			}
		}
		if (all.empty()) {
			i = text.tags.erase(i);
		} else {
			i->id = TextUtilities::JoinTag(all);
			++i;
		}
	}
	_editLinkCallback(selection, text, data.link, EditLinkAction::Edit);
}

void InputField::inputMethodEventInner(QInputMethodEvent *e) {
	const auto preedit = e->preeditString();
	if (_lastPreEditText != preedit) {
		_lastPreEditText = preedit;
		startPlaceholderAnimation();
	}
	_inputMethodCommit = e->commitString();

	const auto weak = MakeWeak(this);
	_inner->QTextEdit::inputMethodEvent(e);

	if (weak && _inputMethodCommit.has_value()) {
		const auto text = *base::take(_inputMethodCommit);
		if (!processMarkdownReplaces(text)) {
			processInstantReplaces(text);
		}
	}
}

const InstantReplaces &InputField::instantReplaces() const {
	return _mutableInstantReplaces;
}

// Disable markdown instant replacement.
bool InputField::processMarkdownReplaces(const QString &appended) {
	//if (appended.size() != 1 || !_markdownEnabled) {
	//	return false;
	//}
	//const auto ch = appended[0];
	//if (ch == '`') {
	//	return processMarkdownReplace(kTagCode)
	//		|| processMarkdownReplace(kTagPre);
	//} else if (ch == '*') {
	//	return processMarkdownReplace(kTagBold);
	//} else if (ch == '_') {
	//	return processMarkdownReplace(kTagItalic);
	//}
	return false;
}

//bool InputField::processMarkdownReplace(const QString &tag) {
//	const auto position = textCursor().position();
//	const auto tagLength = tag.size();
//	const auto start = [&] {
//		for (const auto &possible : _lastMarkdownTags) {
//			const auto end = possible.start + possible.length;
//			if (possible.start + 2 * tagLength >= position) {
//				return MarkdownTag();
//			} else if (end >= position || end + tagLength == position) {
//				if (possible.tag == tag) {
//					return possible;
//				}
//			}
//		}
//		return MarkdownTag();
//	}();
//	if (start.tag.isEmpty()) {
//		return false;
//	}
//	return commitMarkdownReplacement(start.start, position, tag, tag);
//}

void InputField::processInstantReplaces(const QString &appended) {
	const auto &replaces = instantReplaces();
	if (appended.size() != 1
		|| !_instantReplacesEnabled
		|| !replaces.maxLength) {
		return;
	}
	const auto it = replaces.reverseMap.tail.find(appended[0]);
	if (it == end(replaces.reverseMap.tail)) {
		return;
	}
	const auto position = textCursor().position();
	for (const auto &tag : _lastMarkdownTags) {
		if (tag.internalStart < position
			&& tag.internalStart + tag.internalLength >= position
			&& (tag.tag == kTagCode || IsTagPre(tag.tag))) {
			return;
		}
	}
	const auto typed = getTextWithTagsPart(
		std::max(position - replaces.maxLength, 0),
		position - 1).text;
	auto node = &it->second;
	auto i = typed.size();
	do {
		if (!node->text.isEmpty()) {
			applyInstantReplace(typed.mid(i) + appended, node->text);
			return;
		} else if (!i) {
			return;
		}
		const auto it = node->tail.find(typed[--i]);
		if (it == end(node->tail)) {
			return;
		}
		node = &it->second;
	} while (true);
}

void InputField::applyInstantReplace(
		const QString &what,
		const QString &with) {
	const auto length = int(what.size());
	const auto cursor = textCursor();
	const auto position = cursor.position();
	if (cursor.hasSelection()) {
		return;
	} else if (position < length) {
		return;
	}
	commitInstantReplacement(
		position - length,
		position,
		with,
		QString(),
		what,
		true);
}

void InputField::commitInstantReplacement(
		int from,
		int till,
		const QString &with,
		const QString &customEmojiData) {
	commitInstantReplacement(
		from,
		till,
		with,
		customEmojiData,
		std::nullopt,
		false);
}

void InputField::commitInstantReplacement(
		int from,
		int till,
		const QString &with,
		const QString &customEmojiData,
		std::optional<QString> checkOriginal,
		bool checkIfInMonospace) {
	const auto original = getTextWithTagsPart(from, till).text;
	if (checkOriginal
		&& checkOriginal->compare(original, Qt::CaseInsensitive) != 0) {
		return;
	}

	auto cursor = textCursor();
	if (checkIfInMonospace) {
		const auto currentTag = cursor.charFormat().property(
			kTagProperty
		).toString();
		for (const auto &tag : TextUtilities::SplitTags(currentTag)) {
			if (tag == kTagCode || IsTagPre(tag)) {
				return;
			}
		}
	}
	cursor.setPosition(from);
	cursor.setPosition(till, QTextCursor::KeepAnchor);

	const auto link = customEmojiData.isEmpty()
		? QString()
		: CustomEmojiLink(customEmojiData);
	const auto unique = link.isEmpty()
		? QString()
		: MakeUniqueCustomEmojiLink(link);
	auto format = [&]() -> QTextCharFormat {
		auto emojiLength = 0;
		const auto emoji = Emoji::Find(with, &emojiLength);
		if (!emoji || with.size() != emojiLength) {
			return _defaultCharFormat;
		} else if (!customEmojiData.isEmpty()) {
			auto result = QTextCharFormat();
			result.setObjectType(kCustomEmojiFormat);
			result.setProperty(kCustomEmojiText, with);
			result.setProperty(kCustomEmojiLink, unique);
			result.setProperty(kCustomEmojiId, CustomEmojiIdFromLink(link));
			result.setVerticalAlignment(QTextCharFormat::AlignTop);
			return result;
		}
		const auto use = Integration::Instance().defaultEmojiVariant(
			emoji);
		return PrepareEmojiFormat(use, _st.style.font);
	}();
	const auto replacement = (format.isImageFormat()
		|| format.objectType() == kCustomEmojiFormat)
		? kObjectReplacement
		: with;
	format.setProperty(kInstantReplaceWhatId, original);
	format.setProperty(kInstantReplaceWithId, replacement);
	format.setProperty(
		kInstantReplaceRandomId,
		base::RandomValue<uint32>());
	ApplyTagFormat(format, cursor.charFormat());
	if (!unique.isEmpty()) {
		format.setProperty(kTagProperty, TextUtilities::TagWithAdded(
			format.property(kTagProperty).toString(),
			unique));
	}
	cursor.insertText(replacement, format);
}

#if 0
bool InputField::commitMarkdownReplacement(
		int from,
		int till,
		const QString &tag,
		const QString &edge) {
	const auto end = [&] {
		auto cursor = QTextCursor(document());
		cursor.movePosition(QTextCursor::End);
		return cursor.position();
	}();

	// In case of 'pre' tag extend checked text by one symbol.
	// So that we'll know if we need to insert additional newlines.
	// "Test ```test``` Test" should become three-line text.
	const auto blocktag = (tag == kTagPre);
	const auto extendLeft = (blocktag && from > 0) ? 1 : 0;
	const auto extendRight = (blocktag && till < end) ? 1 : 0;
	const auto extended = getTextWithTagsPart(
		from - extendLeft,
		till + extendRight).text;
	const auto outer = base::StringViewMid(
		extended,
		extendLeft,
		extended.size() - extendLeft - extendRight);
	if ((outer.size() <= 2 * edge.size())
		|| (!edge.isEmpty()
			&& !(outer.startsWith(edge) && outer.endsWith(edge)))) {
		return false;
	}

	// In case of 'pre' tag check if we need to remove one of two newlines.
	// "Test\n```\ntest\n```" should become two-line text + newline.
	const auto innerRight = edge.size();
	const auto checkIfTwoNewlines = blocktag
		&& (extendLeft > 0)
		&& IsNewline(extended[0]);
	const auto innerLeft = [&] {
		const auto simple = edge.size();
		if (!checkIfTwoNewlines) {
			return simple;
		}
		const auto last = outer.size() - innerRight;
		for (auto check = simple; check != last; ++check) {
			const auto ch = outer.at(check);
			if (IsNewline(ch)) {
				return check + 1;
			} else if (!Text::IsSpace(ch)) {
				break;
			}
		}
		return simple;
	}();
	const auto innerLength = outer.size() - innerLeft - innerRight;

	// Prepare the final "insert" replacement for the "outer" text part.
	const auto newlineleft = blocktag
		&& (extendLeft > 0)
		&& !IsNewline(extended[0])
		&& !IsNewline(outer.at(innerLeft));
	const auto newlineright = blocktag
		&& (!extendRight || !IsNewline(extended[extended.size() - 1]))
		&& !IsNewline(outer.at(outer.size() - innerRight - 1));
	const auto insert = (newlineleft ? "\n" : "")
		+ outer.mid(innerLeft, innerLength).toString()
		+ (newlineright ? "\n" : "");

	// Trim inserted tag, so that all newlines are left outside.
	_insertedTags.clear();
	auto tagFrom = newlineleft ? 1 : 0;
	auto tagTill = insert.size() - (newlineright ? 1 : 0);
	for (; tagFrom != tagTill; ++tagFrom) {
		const auto ch = insert.at(tagFrom);
		if (!IsNewline(ch)) {
			break;
		}
	}
	for (; tagTill != tagFrom; --tagTill) {
		const auto ch = insert.at(tagTill - 1);
		if (!IsNewline(ch)) {
			break;
		}
	}
	if (tagTill > tagFrom) {
		_insertedTags.push_back({
			tagFrom,
			int(tagTill - tagFrom),
			tag,
		});
	}

	// Replace.
	auto cursor = _inner->textCursor();
	cursor.setPosition(from);
	cursor.setPosition(till, QTextCursor::KeepAnchor);
	auto format = _defaultCharFormat;
	if (!edge.isEmpty()) {
		format.setProperty(kReplaceTagId, edge);
		_reverseMarkdownReplacement = true;
	}
	_insertedTagsAreFromMime = false;
	cursor.insertText(insert, format);
	_insertedTags.clear();

	cursor.setCharFormat(_defaultCharFormat);
	_inner->setTextCursor(cursor);

	// Fire the tag to the spellchecker.
	_markdownTagApplies.fire({ from, till, -1, -1, false, tag });

	return true;
}
#endif

auto InputField::addMarkdownTag(TextRange range, const QString &tag)
-> TextRange {
	auto current = getTextWithTagsPart(range.from, range.till);
	auto filled = 0;
	auto tags = TextWithTags::Tags();
	if (!TextUtilities::IsSeparateTag(tag)) {
		for (auto existing : current.tags) {
			if (existing.offset > filled) {
				tags.push_back({ filled, existing.offset - filled, tag });
			}
			existing.id = TextUtilities::TagWithAdded(existing.id, tag);
			tags.push_back(std::move(existing));
			filled = existing.offset + existing.length;
		}
	}
	if (filled < current.text.size()) {
		tags.push_back({ filled, int(current.text.size()) - filled, tag });
	}
	current.tags = TextUtilities::SimplifyTags(std::move(tags));
	const auto result = insertWithTags(range, std::move(current));

	// Fire the tag to the spellchecker.
	_markdownTagApplies.fire(
		{ result.from, result.till, -1, -1, false, tag });

	return result;
}

auto InputField::insertWithTags(TextRange range, TextWithTags text)
-> TextRange {
	if (text.empty() || text.tags.isEmpty()) {
		finishMarkdownTagChange(range, PrepareForInsert(std::move(text)));
		return range;
	}
	text = ShiftLeftBlockTag(std::move(text));
	text = ShiftRightBlockTag(std::move(text));
	auto result = range;
	const auto firstTag = text.tags.front();
	const auto lastTag = text.tags.back();
	const auto textLength = int(text.text.size());
	const auto adjustLeft = !firstTag.offset && HasBlockTag(firstTag.id);
	const auto adjustRight = (lastTag.offset + lastTag.length >= textLength)
		&& HasBlockTag(lastTag.id);
	auto cursor = QTextCursor(document());
	cursor.movePosition(QTextCursor::End);
	const auto fullLength = cursor.position();
	const auto goodLeft = !adjustLeft
		|| !range.from
		|| (document()->findBlock(range.from).position() == range.from);
	const auto goodRight = !adjustRight
		|| (range.till >= fullLength)
		|| (document()->findBlock(range.till + 1).position()
			== range.till + 1);
	const auto leftEdge = goodLeft
		? TextWithTags()
		: getTextWithTagsPart(range.from - 1, range.from);
	const auto rightEdge = goodRight
		? TextWithTags()
		: getTextWithTagsPart(range.till, range.till + 1);
	const auto extendLeft = !leftEdge.empty()
		&& IsNewline(leftEdge.text.back());
	const auto extendRight = !rightEdge.empty()
		&& IsNewline(rightEdge.text.front());
	if (!goodLeft) {
		text.text.insert(0, kHardLine);
		for (auto &tag : text.tags) {
			++tag.offset;
		}
		if (extendLeft) {
			--range.from;
		} else {
			++result.from;
			++result.till;
		}
	}
	if (!goodRight) {
		text.text.push_back(kHardLine);
		if (extendRight) {
			++range.till;
		}
	}
	finishMarkdownTagChange(range, PrepareForInsert(std::move(text)));
	return result;
}

void InputField::removeMarkdownTag(TextRange range, const QString &tag) {
	auto current = getTextWithTagsPart(range.from, range.till);

	auto tags = TagList();
	for (const auto &existing : current.tags) {
		const auto id = TextUtilities::TagWithRemoved(existing.id, tag);
		const auto additional = (tag == kTagPre)
			? kTagCode
			: (tag == kTagCode)
			? kTagPre
			: QString();
		const auto removeBlock = (IsTagPre(tag) || tag == kTagCode)
			&& IsTagPre(FindBlockTag(id));
		const auto use = removeBlock
			? WithBlockTagRemoved(id)
			: additional.isEmpty()
			? id
			: TextUtilities::TagWithRemoved(id, additional);
		if (!use.isEmpty()) {
			tags.push_back({ existing.offset, existing.length, use });
		}
	}
	current.tags = std::move(tags);

	_insertedTagsReplace = true;
	finishMarkdownTagChange(range, PrepareForInsert(std::move(current)));
	_insertedTagsReplace = false;
}

void InputField::finishMarkdownTagChange(
		TextRange range,
		const TextWithTags &textWithTags) {
	auto cursor = _inner->textCursor();
	cursor.beginEditBlock();
	cursor.setPosition(range.from);
	cursor.setPosition(range.till, QTextCursor::KeepAnchor);
	_insertedTags = textWithTags.tags;
	_realInsertPosition = range.from;
	_realCharsAdded = textWithTags.text.size();
	cursor.insertText(textWithTags.text);

	cursor.setCharFormat(_defaultCharFormat);
	cursor.endEditBlock();

	if (!_insertedTagsDelayClear) {
		_insertedTags.clear();
		_realInsertPosition = -1;
	}

	_inner->setTextCursor(cursor);
}

bool InputField::IsValidMarkdownLink(QStringView link) {
	return ::Ui::IsValidMarkdownLink(link) && !::Ui::IsCustomEmojiLink(link);
}

bool InputField::IsCustomEmojiLink(QStringView link) {
	return ::Ui::IsCustomEmojiLink(link);
}

QString InputField::CustomEmojiLink(QStringView entityData) {
	return MakeUniqueCustomEmojiLink(u"%1%2"_q
		.arg(kCustomEmojiTagStart)
		.arg(entityData));
}

QString InputField::CustomEmojiEntityData(QStringView link) {
	const auto match = qthelp::regex_match(
		"^(\\d+)(\\?|$)",
		base::StringViewMid(link, kCustomEmojiTagStart.size()));
	return match ? match->captured(1) : QString();
}

void InputField::commitMarkdownLinkEdit(
		EditLinkSelection selection,
		const TextWithTags &textWithTags,
		const QString &link) {
	if (textWithTags.text.isEmpty()
		|| !IsValidMarkdownLink(link)
		|| !_editLinkCallback) {
		return;
	}
	auto prepared = PrepareForInsert(textWithTags);
	{
		auto from = 0;
		const auto till = int(prepared.text.size());
		auto &tags = prepared.tags;
		auto i = tags.begin();
		while (from < till) {
			while (i != tags.end() && i->offset <= from) {
				auto all = TextUtilities::SplitTags(i->id);
				auto j = all.begin();
				for (; j != all.end(); ++j) {
					if (IsValidMarkdownLink(*j)) {
						*j = link;
						break;
					}
				}
				if (j == all.end()) {
					all.push_back(link);
				}
				i->id = TextUtilities::JoinTag(all);
				from = i->offset + i->length;
				++i;
			}
			const auto tagFrom = (i == tags.end())
				? till
				: i->offset;
			if (from < tagFrom) {
				i = tags.insert(i, { from, tagFrom - from, link });
				from = tagFrom;
				++i;
			}
		}
	}
	_insertedTags = prepared.tags;
	_insertedTagsAreFromMime = false;

	auto cursor = textCursor();
	const auto editData = selectionEditLinkData(selection);
	cursor.setPosition(editData.from);
	cursor.setPosition(editData.till, QTextCursor::KeepAnchor);
	auto format = _defaultCharFormat;
	_insertedTagsAreFromMime = false;
	const auto text = prepared.text;
	cursor.insertText(
		(editData.from == editData.till) ? (text + QChar(' ')) : text,
		_defaultCharFormat);
	_insertedTags.clear();

	_reverseMarkdownReplacement = false;
	_correcting = true;
	cursor.joinPreviousEditBlock();
	cursor.setCharFormat(_defaultCharFormat);
	cursor.endEditBlock();
	_inner->setTextCursor(cursor);
	_correcting = false;
}

void InputField::toggleSelectionMarkdown(const QString &tag) {
	_reverseMarkdownReplacement = false;
	_insertedTagsAreFromMime = false;
	const auto cursor = textCursor();
	auto position = cursor.position();
	auto from = cursor.selectionStart();
	auto till = cursor.selectionEnd();
	if (from >= till) {
		return;
	}
	if (document()->characterAt(from) == kHardLine) {
		++from;
	}
	if (document()->characterAt(till - 1) == kHardLine) {
		--till;
	}
	auto range = TextRange{ from, till };
	if (tag.isEmpty()) {
		RemoveDocumentTags(_st, document(), from, till);
	} else if (HasFullTextTag(getTextWithTagsSelected(), tag)) {
		removeMarkdownTag(range, tag);
	} else {
		const auto leftForBlock = [&] {
			if (from <= 0) {
				return true;
			}
			const auto text = getTextWithTagsPart(
				from - 1,
				from + 1
			).text;
			return text.isEmpty()
				|| IsNewline(text[0])
				|| IsNewline(text[text.size() - 1]);
		}();
		const auto rightForBlock = [&] {
			auto cursor = QTextCursor(document());
			cursor.movePosition(QTextCursor::End);
			if (till >= cursor.position()) {
				return true;
			}
			const auto text = getTextWithTagsPart(
				till - 1,
				till + 1
			).text;
			return text.isEmpty()
				|| IsNewline(text[0])
				|| IsNewline(text[text.size() - 1]);
		}();

		const auto useTag = (tag != kTagCode)
			? tag
			: (leftForBlock && rightForBlock)
			? kTagPre
			: kTagCode;
		range = addMarkdownTag(range, useTag);
	}
	auto restorePosition = textCursor();
	restorePosition.setPosition(
		(position == till) ? range.from : range.till);
	restorePosition.setPosition(
		(position == till) ? range.till : range.from,
		QTextCursor::KeepAnchor);
	setTextCursor(restorePosition);
}

void InputField::clearSelectionMarkdown() {
	toggleSelectionMarkdown(QString());
}

bool InputField::revertFormatReplace() {
	const auto cursor = textCursor();
	const auto position = cursor.position();
	if (position <= 0 || cursor.hasSelection()) {
		return false;
	}
	const auto inside = position - 1;
	const auto document = _inner->document();
	const auto block = document->findBlock(inside);
	if (block == document->end()) {
		return false;
	}
	for (auto i = block.begin(); !i.atEnd(); ++i) {
		const auto fragment = i.fragment();
		const auto fragmentStart = fragment.position();
		const auto fragmentEnd = fragmentStart + fragment.length();
		if (fragmentEnd <= inside) {
			continue;
		} else if (fragmentStart > inside || fragmentEnd != position) {
			return false;
		}
		const auto current = fragment.charFormat();
		if (current.hasProperty(kInstantReplaceWithId)) {
			const auto with = current.property(kInstantReplaceWithId);
			const auto string = with.toString();
			if (fragment.text() != string) {
				return false;
			}
			auto replaceCursor = cursor;
			replaceCursor.setPosition(fragmentStart);
			replaceCursor.setPosition(fragmentEnd, QTextCursor::KeepAnchor);
			const auto what = current.property(kInstantReplaceWhatId);
			auto format = _defaultCharFormat;
			ApplyTagFormat(format, current);
			replaceCursor.insertText(what.toString(), format);
			return true;
		} else if (_reverseMarkdownReplacement
			&& current.hasProperty(kReplaceTagId)) {
			const auto tag = current.property(kReplaceTagId).toString();
			if (tag.isEmpty()) {
				return false;
			} else if (auto test = i; !(++test).atEnd()) {
				const auto format = test.fragment().charFormat();
				if (format.property(kReplaceTagId).toString() == tag) {
					return false;
				}
			} else if (auto test = block; test.next() != document->end()) {
				const auto begin = test.begin();
				if (begin != test.end()) {
					const auto format = begin.fragment().charFormat();
					if (format.property(kReplaceTagId).toString() == tag) {
						return false;
					}
				}
			}

			const auto first = [&] {
				auto checkBlock = block;
				auto checkLast = i;
				while (true) {
					for (auto j = checkLast; j != checkBlock.begin();) {
						--j;
						const auto format = j.fragment().charFormat();
						if (format.property(kReplaceTagId) != tag) {
							return ++j;
						}
					}
					if (checkBlock == document->begin()) {
						return checkBlock.begin();
					}
					checkBlock = checkBlock.previous();
					checkLast = checkBlock.end();
				}
			}();
			const auto from = first.fragment().position();
			const auto till = fragmentEnd;
			auto replaceCursor = cursor;
			replaceCursor.setPosition(from);
			replaceCursor.setPosition(till, QTextCursor::KeepAnchor);
			replaceCursor.insertText(
				tag + getTextWithTagsPart(from, till).text + tag,
				_defaultCharFormat);
			return true;
		}
		return false;
	}
	return false;
}

bool InputField::jumpOutOfBlockByBackspace() {
	auto cursor = textCursor();
	if (cursor.hasSelection()) {
		return false;
	}
	const auto position = cursor.position();
	if (!position) {
		return false;
	}
	const auto block = document()->findBlock(position);
	const auto tagId = block.blockFormat().property(kQuoteFormatId);
	if (block.position() != position || !HasBlockTag(tagId.toString())) {
		return false;
	}
	cursor.setPosition(position - 1);
	setTextCursor(cursor);
	return true;
}

void InputField::contextMenuEventInner(QContextMenuEvent *e, QMenu *m) {
	if (const auto menu = m ? m : _inner->createStandardContextMenu()) {
		addMarkdownActions(menu, e);
		_contextMenu = base::make_unique_q<PopupMenu>(this, menu, _st.menu);
		QObject::connect(_contextMenu.get(), &QObject::destroyed, [=] {
			_menuShownChanges.fire(false);
		});
		_menuShownChanges.fire(true);
		_contextMenu->popup(e->globalPos());
	}
}

void InputField::addMarkdownActions(
		not_null<QMenu*> menu,
		QContextMenuEvent *e) {
	if (_markdownEnabledState.disabled()) {
		return;
	}
	auto &integration = Integration::Instance();

	const auto formatting = new QAction(
		integration.phraseFormattingTitle(),
		menu);
	addMarkdownMenuAction(menu, formatting);

	const auto submenu = new QMenu(menu);
	formatting->setMenu(submenu);

	const auto textWithTags = getTextWithTagsSelected();
	const auto &text = textWithTags.text;
	const auto &tags = textWithTags.tags;
	const auto hasText = !text.isEmpty();
	const auto hasTags = !tags.isEmpty();
	const auto disabled = (!_editLinkCallback && !hasText);
	formatting->setDisabled(disabled);
	if (disabled) {
		return;
	}
	const auto add = [&](
			const QString &base,
			QKeySequence sequence,
			bool disabled,
			auto callback) {
		const auto add = sequence.isEmpty()
			? QString()
			: QChar('\t') + sequence.toString(QKeySequence::NativeText);
		const auto action = new QAction(base + add, submenu);
		connect(action, &QAction::triggered, this, callback);
		action->setDisabled(disabled);
		submenu->addAction(action);
	};
	const auto addtag = [&](
			const QString &base,
			QKeySequence sequence,
			const QString &tag) {
		if (!_markdownEnabledState.enabledForTag(tag)) {
			return;
		}
		const auto disabled = !hasText;
		add(base, sequence, disabled, [=] {
			toggleSelectionMarkdown(tag);
		});
	};
	const auto addlink = [&] {
		const auto selection = editLinkSelection(e);
		const auto data = selectionEditLinkData(selection);
		const auto base = data.link.isEmpty()
			? integration.phraseFormattingLinkCreate()
			: integration.phraseFormattingLinkEdit();
		add(base, kEditLinkSequence, false, [=] {
			editMarkdownLink(selection);
		});
	};
	const auto addclear = [&] {
		const auto disabled = !hasText || !hasTags;
		add(integration.phraseFormattingClear(), kClearFormatSequence, disabled, [=] {
			clearSelectionMarkdown();
		});
	};

	addtag(integration.phraseFormattingBold(), QKeySequence::Bold, kTagBold);
	addtag(integration.phraseFormattingItalic(), QKeySequence::Italic, kTagItalic);
	addtag(integration.phraseFormattingUnderline(), QKeySequence::Underline, kTagUnderline);
	addtag(integration.phraseFormattingStrikeOut(), kStrikeOutSequence, kTagStrikeOut);
	addtag(integration.phraseFormattingBlockquote(), kBlockquoteSequence, kTagBlockquote);
	addtag(integration.phraseFormattingMonospace(), kMonospaceSequence, kTagCode);
	addtag(integration.phraseFormattingSpoiler(), kSpoilerSequence, kTagSpoiler);

	if (_editLinkCallback) {
		submenu->addSeparator();
		addlink();
	}

	submenu->addSeparator();
	addclear();
}

void InputField::addMarkdownMenuAction(
		not_null<QMenu*> menu,
		not_null<QAction*> action) {
	const auto actions = menu->actions();
	const auto before = [&] {
		auto seenAfter = false;
		for (const auto action : actions) {
			if (seenAfter) {
				return action;
			} else if (action->objectName() == qstr("edit-delete")) {
				seenAfter = true;
			}
		}
		return (QAction*)nullptr;
	}();
	menu->insertSeparator(before);
	menu->insertAction(before, action);
}

void InputField::dropEventInner(QDropEvent *e) {
	_insertedTagsDelayClear = true;
	_inner->QTextEdit::dropEvent(e);
	_insertedTagsDelayClear = false;
	_insertedTags.clear();
	_realInsertPosition = -1;
	window()->raise();
	window()->activateWindow();
}

bool InputField::canInsertFromMimeDataInner(const QMimeData *source) const {
	if (source
		&& _mimeDataHook
		&& _mimeDataHook(source, MimeAction::Check)) {
		return true;
	}
	return _inner->QTextEdit::canInsertFromMimeData(source);
}

void InputField::insertFromMimeDataInner(const QMimeData *source) {
	if (source
		&& _mimeDataHook
		&& _mimeDataHook(source, MimeAction::Insert)) {
		return;
	}
	const auto text = [&] {
		const auto textMime = TextUtilities::TagsTextMimeType();
		const auto tagsMime = TextUtilities::TagsMimeType();
		if (!source->hasFormat(textMime) || !source->hasFormat(tagsMime)) {
			_insertedTags.clear();

			auto result = source->text();
			return result.replace(u"\r\n"_q, u"\n"_q);
		}
		auto result = QString::fromUtf8(source->data(textMime));
		_insertedTags = TextUtilities::DeserializeTags(
			source->data(tagsMime),
			result.size());
		_insertedTagsAreFromMime = true;
		return result;
	}();
	auto cursor = textCursor();
	if (!text.isEmpty()) {
		insertWithTags(
			{ cursor.selectionStart(), cursor.selectionEnd() },
			{ text, _insertedTags });
	}
	ensureCursorVisible();
	if (!_insertedTagsDelayClear) {
		_insertedTags.clear();
	}
}

void InputField::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder(_placeholderFull.current());
	_inner->setGeometry(rect().marginsRemoved(
		_st.textMargins + _additionalMargins + _customFontMargins));
	if (_spoilerOverlay) {
		_spoilerOverlay->setGeometry(_inner->rect());
	}
	_borderAnimationStart = width() / 2;
	RpWidget::resizeEvent(e);
	checkContentHeight();
}

void InputField::refreshPlaceholder(const QString &text) {
	const auto margins = _st.textMargins
		+ _st.placeholderMargins
		+ _additionalMargins
		+ _customFontMargins;
	const auto availableWidth = rect().marginsRemoved(margins).width();
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		const auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(text, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			const auto result = style::FindAdjustResult(placeholderFont);
			const auto ascent = result ? result->iascent : metrics.ascent();
			_placeholderPath.addText(0, ascent, placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(text, availableWidth);
	}
	update();
}

void InputField::setPlaceholder(
		rpl::producer<QString> placeholder,
		int afterSymbols) {
	_placeholderFull = std::move(placeholder);
	if (_placeholderAfterSymbols != afterSymbols) {
		_placeholderAfterSymbols = afterSymbols;
		startPlaceholderAnimation();
	}
}

void InputField::setEditLinkCallback(
	Fn<bool(
		EditLinkSelection selection,
		TextWithTags text,
		QString link,
		EditLinkAction action)> callback) {
	_editLinkCallback = std::move(callback);
}

void InputField::setEditLanguageCallback(
		Fn<void(QString now, Fn<void(QString)> save)> callback) {
	_editLanguageCallback = std::move(callback);
}

void InputField::showError() {
	showErrorNoFocus();
	if (!hasFocus()) {
		_inner->setFocus();
	}
}

void InputField::showErrorNoFocus() {
	setErrorShown(true);
}

void InputField::hideError() {
	setErrorShown(false);
}

void InputField::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start([this] { update(); }, _error ? 0. : 1., _error ? 1. : 0., _st.duration);
		startBorderAnimation();
	}
}

rpl::producer<> InputField::heightChanges() const {
	return _heightChanges.events();
}

rpl::producer<bool> InputField::focusedChanges() const {
	return _focusedChanges.events();
}

rpl::producer<> InputField::tabbed() const {
	return _tabbed.events();
}

rpl::producer<> InputField::cancelled() const {
	return _cancelled.events();
}

rpl::producer<> InputField::changes() const {
	return _changes.events();
}

rpl::producer<Qt::KeyboardModifiers> InputField::submits() const {
	return _submits.events();
}

InputField::~InputField() = default;

// Optimization: with null page size document does not re-layout
// on each insertText / mergeCharFormat.
void PrepareFormattingOptimization(not_null<QTextDocument*> document) {
	if (!document->pageSize().isNull()) {
		document->setPageSize(QSizeF(0, 0));
	}
}

int ComputeRealUnicodeCharactersCount(const QString &text) {
	return text.size() - ranges::count(text, true, [](QChar ch) {
		return ch.isHighSurrogate();
	});
}

int ComputeFieldCharacterCount(not_null<InputField*> field) {
	return ComputeRealUnicodeCharactersCount(field->getLastText());
}

void AddLengthLimitLabel(
		not_null<InputField*> field,
		int limit,
		std::optional<uint> customThreshold,
		int limitLabelTop) {
	struct State {
		rpl::variable<int> length;
	};
	constexpr auto kMinus = QChar(0x2212);
	const auto state = field->lifetime().make_state<State>();
	state->length = rpl::single(
		rpl::empty
	) | rpl::then(field->changes()) | rpl::map([=] {
		return int(field->getLastText().size());
	});
	const auto allowExceed = std::max(limit / 2, 9);
	field->setMaxLength(limit + allowExceed);
	const auto threshold = !customThreshold
		? std::min(limit / 2, 9)
		: int(*customThreshold);
	auto warningText = state->length.value() | rpl::map([=](int count) {
		const auto left = limit - count;
		return (left >= threshold)
			? QString()
			: (left < 0)
			? (kMinus + QString::number(std::abs(left)))
			: QString::number(left);
	});
	const auto warning = CreateChild<FlatLabel>(
		field.get(),
		std::move(warningText),
		st::defaultInputFieldLimit);

	const auto maxSize = st::defaultInputFieldLimit.style.font->width(
		kMinus + QString::number(allowExceed));
	const auto add = std::max(maxSize - field->st().textMargins.right(), 0);
	if (add) {
		field->setAdditionalMargins({ 0, 0, add, 0 });
	}
	state->length.value() | rpl::map(
		rpl::mappers::_1 > limit
	) | rpl::start_with_next([=](bool exceeded) {
		warning->setTextColorOverride(exceeded
			? st::attentionButtonFg->c
			: std::optional<QColor>());
	}, warning->lifetime());
	rpl::combine(
		field->sizeValue(),
		warning->sizeValue()
	) | rpl::start_with_next([=] {
		// Baseline alignment.
		const auto top = field->st().textMargins.top()
			+ field->st().style.font->ascent
			- st::defaultInputFieldLimit.style.font->ascent;
		warning->moveToRight(0, top + limitLabelTop);
	}, warning->lifetime());
	warning->setAttribute(Qt::WA_TransparentForMouseEvents);
}

} // namespace Ui
