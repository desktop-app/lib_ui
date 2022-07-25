// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text.h"

#include "ui/basic_click_handlers.h"
#include "ui/text/text_block.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/emoji_config.h"
#include "ui/integration.h"
#include "ui/round_rect.h"
#include "ui/image/image_prepare.h"
#include "ui/spoiler_click_handler.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_common_adapters.h"

#include <private/qfontengine_p.h>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <private/qharfbuzz_p.h>
#endif // Qt < 6.0.0

namespace Ui::Text {
namespace {

constexpr auto kStringLinkIndexShift = uint16(0x8000);
constexpr auto kMaxDiacAfterSymbol = 2;

inline bool IsMono(int32 flags) {
	return (flags & TextBlockFPre) || (flags & TextBlockFCode);
}

Qt::LayoutDirection StringDirection(
		const QString &str,
		int from,
		int to) {
	auto p = reinterpret_cast<const ushort*>(str.unicode()) + from;
	const auto end = p + (to - from);
	while (p < end) {
		uint ucs4 = *p;
		if (QChar::isHighSurrogate(ucs4) && p < end - 1) {
			ushort low = p[1];
			if (QChar::isLowSurrogate(low)) {
				ucs4 = QChar::surrogateToUcs4(ucs4, low);
				++p;
			}
		}
		switch (QChar::direction(ucs4)) {
		case QChar::DirL:
			return Qt::LeftToRight;
		case QChar::DirR:
		case QChar::DirAL:
			return Qt::RightToLeft;
		default:
			break;
		}
		++p;
	}
	return Qt::LayoutDirectionAuto;
}

TextWithEntities PrepareRichFromRich(
		const TextWithEntities &text,
		const TextParseOptions &options) {
	auto result = text;
	const auto &preparsed = text.entities;
	const bool parseLinks = (options.flags & TextParseLinks);
	const bool parsePlainLinks = (options.flags & TextParsePlainLinks);
	if (!preparsed.isEmpty() && (parseLinks || parsePlainLinks)) {
		bool parseMentions = (options.flags & TextParseMentions);
		bool parseHashtags = (options.flags & TextParseHashtags);
		bool parseBotCommands = (options.flags & TextParseBotCommands);
		bool parseMarkdown = (options.flags & TextParseMarkdown);
		if (!parseMentions || !parseHashtags || !parseBotCommands || !parseMarkdown) {
			int32 i = 0, l = preparsed.size();
			result.entities.clear();
			result.entities.reserve(l);
			for (; i < l; ++i) {
				auto type = preparsed.at(i).type();
				if (((type == EntityType::Mention || type == EntityType::MentionName) && !parseMentions) ||
					(type == EntityType::Hashtag && !parseHashtags) ||
					(type == EntityType::Cashtag && !parseHashtags) ||
					(type == EntityType::PlainLink
						&& !parsePlainLinks
						&& !parseMarkdown) ||
					(!parseLinks
						&& (type == EntityType::Url
							|| type == EntityType::CustomUrl)) ||
					(type == EntityType::BotCommand && !parseBotCommands) || // #TODO entities
					(!parseMarkdown && (type == EntityType::Bold
						|| type == EntityType::Semibold
						|| type == EntityType::Italic
						|| type == EntityType::Underline
						|| type == EntityType::StrikeOut
						|| type == EntityType::Code
						|| type == EntityType::Pre))) {
					continue;
				}
				result.entities.push_back(preparsed.at(i));
			}
		}
	}
	return result;
}

QFixed ComputeStopAfter(
		const TextParseOptions &options,
		const style::TextStyle &st) {
	return (options.maxw > 0 && options.maxh > 0)
		? ((options.maxh / st.font->height) + 1) * options.maxw
		: QFIXED_MAX;
}

// Open Sans tilde fix.
bool ComputeCheckTilde(const style::TextStyle &st) {
	const auto &font = st.font;
	return (font->size() * style::DevicePixelRatio() == 13)
		&& (font->flags() == 0)
		&& (font->f.family() == qstr("DAOpenSansRegular"));
}

bool IsParagraphSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::LineFeed:
		return true;
	default:
		break;
	}
	return false;
}

bool IsBad(QChar ch) {
	return (ch == 0)
		|| (ch >= 8232 && ch < 8237)
		|| (ch >= 65024 && ch < 65040 && ch != 65039)
		|| (ch >= 127 && ch < 160 && ch != 156)

		// qt harfbuzz crash see https://github.com/telegramdesktop/tdesktop/issues/4551
		|| (Platform::IsMac() && ch == 6158);
}

void InitTextItemWithScriptItem(QTextItemInt &ti, const QScriptItem &si) {
	// explicitly initialize flags so that initFontAttributes can be called
	// multiple times on the same TextItem
	ti.flags = { };
	if (si.analysis.bidiLevel %2)
		ti.flags |= QTextItem::RightToLeft;
	ti.ascent = si.ascent;
	ti.descent = si.descent;

	if (ti.charFormat.hasProperty(QTextFormat::TextUnderlineStyle)) {
		ti.underlineStyle = ti.charFormat.underlineStyle();
	} else if (ti.charFormat.boolProperty(QTextFormat::FontUnderline)
				|| ti.f->underline()) {
		ti.underlineStyle = QTextCharFormat::SingleUnderline;
	}

	// compat
	if (ti.underlineStyle == QTextCharFormat::SingleUnderline)
		ti.flags |= QTextItem::Underline;

	if (ti.f->overline() || ti.charFormat.fontOverline())
		ti.flags |= QTextItem::Overline;
	if (ti.f->strikeOut() || ti.charFormat.fontStrikeOut())
		ti.flags |= QTextItem::StrikeOut;
}

} // namespace
} // namespace Ui::Text

const TextParseOptions kDefaultTextOptions = {
	TextParseLinks | TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions kMarkupTextOptions = {
	TextParseLinks | TextParseMultiline | TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions kPlainTextOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

namespace Ui {
namespace Text {

class Parser {
public:
	Parser(
		not_null<String*> string,
		const TextWithEntities &textWithEntities,
		const TextParseOptions &options,
		const std::any &context);

private:
	struct ReadyToken {
	};

	class StartedEntity {
	public:
		enum class Type {
			Flags,
			Link,
			IndexedLink,
			Spoiler,
			CustomEmoji,
		};

		explicit StartedEntity(TextBlockFlags flags);
		explicit StartedEntity(uint16 index, Type type);

		[[nodiscard]] Type type() const;
		[[nodiscard]] std::optional<TextBlockFlags> flags() const;
		[[nodiscard]] std::optional<uint16> lnkIndex() const;
		[[nodiscard]] std::optional<uint16> spoilerIndex() const;

	private:
		const int _value = 0;
		const Type _type;

	};

	Parser(
		not_null<String*> string,
		TextWithEntities &&source,
		const TextParseOptions &options,
		const std::any &context,
		ReadyToken);

	void trimSourceRange();
	void blockCreated();
	void createBlock(int32 skipBack = 0);
	void createSkipBlock(int32 w, int32 h);
	void createNewlineBlock();

	// Returns true if at least one entity was parsed in the current position.
	bool checkEntities();
	void parseCurrentChar();
	void parseEmojiFromCurrent();
	void finalize(const TextParseOptions &options);

	void finishEntities();
	void skipPassedEntities();
	void skipBadEntities();

	bool isInvalidEntity(const EntityInText &entity) const;
	bool isLinkEntity(const EntityInText &entity) const;

	bool processCustomIndex(uint16 index);

	void parse(const TextParseOptions &options);
	void computeLinkText(
		const QString &linkData,
		QString *outLinkText,
		EntityLinkShown *outShown);

	const not_null<String*> _t;
	const TextWithEntities _source;
	const std::any &_context;
	const QChar * const _start = nullptr;
	const QChar *_end = nullptr; // mutable, because we trim by decrementing.
	const QChar *_ptr = nullptr;
	const EntitiesInText::const_iterator _entitiesEnd;
	EntitiesInText::const_iterator _waitingEntity;
	QString _customEmojiData;
	const bool _multiline = false;

	const QFixed _stopAfterWidth; // summary width of all added words
	const bool _checkTilde = false; // do we need a special text block for tilde symbol

	std::vector<uint16> _linksIndexes;

	std::vector<EntityLinkData> _links;
	std::vector<EntityLinkData> _spoilers;
	std::vector<EntityLinkData> _monos;
	base::flat_map<
		const QChar*,
		std::vector<StartedEntity>> _startedEntities;

	uint16 _maxLnkIndex = 0;
	uint16 _maxShiftedLnkIndex = 0;

	// current state
	int32 _flags = 0;
	uint16 _lnkIndex = 0;
	uint16 _spoilerIndex = 0;
	uint16 _monoIndex = 0;
	EmojiPtr _emoji = nullptr; // current emoji, if current word is an emoji, or zero
	int32 _blockStart = 0; // offset in result, from which current parsed block is started
	int32 _diacs = 0; // diac chars skipped without good char
	QFixed _sumWidth;
	bool _sumFinished = false;
	bool _newlineAwaited = false;

	// current char data
	QChar _ch; // current char (low surrogate, if current char is surrogate pair)
	int32 _emojiLookback = 0; // how far behind the current ptr to look for current emoji
	bool _lastSkipped = false; // did we skip current char

};

Parser::StartedEntity::StartedEntity(TextBlockFlags flags)
: _value(flags)
, _type(Type::Flags) {
	Expects(_value >= 0 && _value < int(kStringLinkIndexShift));
}

Parser::StartedEntity::StartedEntity(uint16 index, Type type)
: _value(index)
, _type(type) {
	Expects((_type == Type::Link)
		? (_value >= kStringLinkIndexShift)
		: (_value < kStringLinkIndexShift));
}

Parser::StartedEntity::Type Parser::StartedEntity::type() const {
	return _type;
}

std::optional<TextBlockFlags> Parser::StartedEntity::flags() const {
	if (_value < int(kStringLinkIndexShift) && (_type == Type::Flags)) {
		return TextBlockFlags(_value);
	}
	return std::nullopt;
}

std::optional<uint16> Parser::StartedEntity::lnkIndex() const {
	if ((_value < int(kStringLinkIndexShift) && (_type == Type::IndexedLink))
		|| (_value >= int(kStringLinkIndexShift) && (_type == Type::Link))) {
		return uint16(_value);
	}
	return std::nullopt;
}

std::optional<uint16> Parser::StartedEntity::spoilerIndex() const {
	if (_value < int(kStringLinkIndexShift) && (_type == Type::Spoiler)) {
		return uint16(_value);
	}
	return std::nullopt;
}

Parser::Parser(
	not_null<String*> string,
	const TextWithEntities &textWithEntities,
	const TextParseOptions &options,
	const std::any &context)
: Parser(
	string,
	PrepareRichFromRich(textWithEntities, options),
	options,
	context,
	ReadyToken()) {
}

Parser::Parser(
	not_null<String*> string,
	TextWithEntities &&source,
	const TextParseOptions &options,
	const std::any &context,
	ReadyToken)
: _t(string)
, _source(std::move(source))
, _context(context)
, _start(_source.text.constData())
, _end(_start + _source.text.size())
, _ptr(_start)
, _entitiesEnd(_source.entities.end())
, _waitingEntity(_source.entities.begin())
, _multiline(options.flags & TextParseMultiline)
, _stopAfterWidth(ComputeStopAfter(options, *_t->_st))
, _checkTilde(ComputeCheckTilde(*_t->_st)) {
	parse(options);
}

void Parser::blockCreated() {
	_sumWidth += _t->_blocks.back()->f_width();
	if (_sumWidth.floor().toInt() > _stopAfterWidth) {
		_sumFinished = true;
	}
}

void Parser::createBlock(int32 skipBack) {
	if (_lnkIndex < kStringLinkIndexShift && _lnkIndex > _maxLnkIndex) {
		_maxLnkIndex = _lnkIndex;
	}
	if (_lnkIndex > kStringLinkIndexShift) {
		_maxShiftedLnkIndex = std::max(
			uint16(_lnkIndex - kStringLinkIndexShift),
			_maxShiftedLnkIndex);
	}

	int32 len = int32(_t->_text.size()) + skipBack - _blockStart;
	if (len > 0) {
		bool newline = !_emoji && (len == 1 && _t->_text.at(_blockStart) == QChar::LineFeed);
		if (_newlineAwaited) {
			_newlineAwaited = false;
			if (!newline) {
				_t->_text.insert(_blockStart, QChar::LineFeed);
				createBlock(skipBack - len);
			}
		}
		_lastSkipped = false;
		const auto lnkIndex = _monoIndex ? _monoIndex : _lnkIndex;
		auto custom = _customEmojiData.isEmpty()
			? nullptr
			: Integration::Instance().createCustomEmoji(
				_customEmojiData,
				_context);
		if (custom) {
			_t->_blocks.push_back(Block::CustomEmoji(_t->_st->font, _t->_text, _blockStart, len, _flags, lnkIndex, _spoilerIndex, std::move(custom)));
			_lastSkipped = true;
		} else if (_emoji) {
			_t->_blocks.push_back(Block::Emoji(_t->_st->font, _t->_text, _blockStart, len, _flags, lnkIndex, _spoilerIndex, _emoji));
			_lastSkipped = true;
		} else if (newline) {
			_t->_blocks.push_back(Block::Newline(_t->_st->font, _t->_text, _blockStart, len, _flags, lnkIndex, _spoilerIndex));
		} else {
			_t->_blocks.push_back(Block::Text(_t->_st->font, _t->_text, _t->_minResizeWidth, _blockStart, len, _flags, lnkIndex, _spoilerIndex));
		}
		_blockStart += len;
		_customEmojiData = QByteArray();
		_emoji = nullptr;
		blockCreated();
	}
}

void Parser::createSkipBlock(int32 w, int32 h) {
	createBlock();
	_t->_text.push_back('_');
	_t->_blocks.push_back(Block::Skip(_t->_st->font, _t->_text, _blockStart++, w, h, _monoIndex ? _monoIndex : _lnkIndex, _spoilerIndex));
	blockCreated();
}

void Parser::createNewlineBlock() {
	createBlock();
	_t->_text.push_back(QChar::LineFeed);
	createBlock();
}

void Parser::finishEntities() {
	while (!_startedEntities.empty()
		&& (_ptr >= _startedEntities.begin()->first || _ptr >= _end)) {
		auto list = std::move(_startedEntities.begin()->second);
		_startedEntities.erase(_startedEntities.begin());

		while (!list.empty()) {
			if (list.back().type() == StartedEntity::Type::CustomEmoji) {
				createBlock();
			} else if (const auto flags = list.back().flags()) {
				if (_flags & (*flags)) {
					createBlock();
					_flags &= ~(*flags);
					if (((*flags) & TextBlockFPre)
						&& !_t->_blocks.empty()
						&& _t->_blocks.back()->type() != TextBlockTNewline) {
						_newlineAwaited = true;
					}
					if (IsMono(*flags)) {
						_monoIndex = 0;
					}
				}
			} else if (const auto lnkIndex = list.back().lnkIndex()) {
				if (_lnkIndex == *lnkIndex) {
					createBlock();
					_lnkIndex = 0;
				}
			} else if (const auto spoilerIndex = list.back().spoilerIndex()) {
				if (_spoilerIndex == *spoilerIndex && (_spoilerIndex != 0)) {
					createBlock();
					_spoilerIndex = 0;
				}
			}
			list.pop_back();
		}
	}
}

// Returns true if at least one entity was parsed in the current position.
bool Parser::checkEntities() {
	finishEntities();
	skipPassedEntities();
	if (_waitingEntity == _entitiesEnd
		|| _ptr < _start + _waitingEntity->offset()) {
		return false;
	}

	auto flags = TextBlockFlags();
	auto link = EntityLinkData();
	auto monoIndex = 0;
	const auto entityType = _waitingEntity->type();
	const auto entityLength = _waitingEntity->length();
	const auto entityBegin = _start + _waitingEntity->offset();
	const auto entityEnd = entityBegin + entityLength;
	const auto pushSimpleUrl = [&](EntityType type) {
		link.type = type;
		link.data = QString(entityBegin, entityLength);
		if (type == EntityType::Url) {
			computeLinkText(link.data, &link.text, &link.shown);
		} else {
			link.text = link.data;
		}
	};
	const auto pushComplexUrl = [&] {
		link.type = entityType;
		link.data = _waitingEntity->data();
		link.text = QString(entityBegin, entityLength);
	};

	using Type = StartedEntity::Type;

	if (entityType == EntityType::CustomEmoji) {
		createBlock();
		_customEmojiData = _waitingEntity->data();
		_startedEntities[entityEnd].emplace_back(0, Type::CustomEmoji);
	} else if (entityType == EntityType::Bold) {
		flags = TextBlockFBold;
	} else if (entityType == EntityType::Semibold) {
		flags = TextBlockFSemibold;
	} else if (entityType == EntityType::Italic) {
		flags = TextBlockFItalic;
	} else if (entityType == EntityType::Underline) {
		flags = TextBlockFUnderline;
	} else if (entityType == EntityType::PlainLink) {
		flags = TextBlockFPlainLink;
	} else if (entityType == EntityType::StrikeOut) {
		flags = TextBlockFStrikeOut;
	} else if ((entityType == EntityType::Code) // #TODO entities
		|| (entityType == EntityType::Pre)) {
		if (entityType == EntityType::Code) {
			flags = TextBlockFCode;
		} else {
			flags = TextBlockFPre;
			createBlock();
			if (!_t->_blocks.empty()
				&& _t->_blocks.back()->type() != TextBlockTNewline
				&& _customEmojiData.isEmpty()) {
				createNewlineBlock();
			}
		}
		const auto text = QString(entityBegin, entityLength);

		// It is better to trim the text to identify "Sample\n" as inline.
		const auto trimmed = text.trimmed();
		const auto isSingleLine = !trimmed.isEmpty()
			&& ranges::none_of(trimmed, IsNewline);

		// TODO: remove trimming.
		if (isSingleLine && (entityType == EntityType::Code)) {
			_monos.push_back({ .text = text, .type = entityType });
			monoIndex = _monos.size();
		}
	} else if (entityType == EntityType::Url
		|| entityType == EntityType::Email
		|| entityType == EntityType::Mention
		|| entityType == EntityType::Hashtag
		|| entityType == EntityType::Cashtag
		|| entityType == EntityType::BotCommand) {
		pushSimpleUrl(entityType);
	} else if (entityType == EntityType::CustomUrl) {
		const auto url = _waitingEntity->data();
		const auto text = QString(entityBegin, entityLength);
		if (url == text) {
			pushSimpleUrl(EntityType::Url);
		} else {
			pushComplexUrl();
		}
	} else if (entityType == EntityType::MentionName) {
		pushComplexUrl();
	}

	if (link.type != EntityType::Invalid) {
		createBlock();

		_links.push_back(link);
		const auto tempIndex = _links.size();
		const auto useCustom = processCustomIndex(tempIndex);
		_lnkIndex = tempIndex + (useCustom ? 0 : kStringLinkIndexShift);
		_startedEntities[entityEnd].emplace_back(
			_lnkIndex,
			useCustom ? Type::IndexedLink : Type::Link);
	} else if (flags) {
		if (!(_flags & flags)) {
			createBlock();
			_flags |= flags;
			_startedEntities[entityEnd].emplace_back(flags);
			_monoIndex = monoIndex;
		}
	} else if (entityType == EntityType::Spoiler) {
		createBlock();

		_spoilers.push_back(EntityLinkData{
			.data = QString::number(_spoilers.size() + 1),
			.type = entityType,
		});
		_spoilerIndex = _spoilers.size();

		_startedEntities[entityEnd].emplace_back(
			_spoilerIndex,
			Type::Spoiler);
	}

	++_waitingEntity;
	skipBadEntities();
	return true;
}

bool Parser::processCustomIndex(uint16 index) {
	auto &url = _links[index - 1].data;
	if (url.isEmpty()) {
		return false;
	}
	if (url.startsWith("internal:index")) {
		const auto customIndex = uint16(url.back().unicode());
		// if (customIndex != index) {
			url = QString();
			_linksIndexes.push_back(customIndex);
			return true;
		// }
	}
	return false;
}

void Parser::skipPassedEntities() {
	while (_waitingEntity != _entitiesEnd
		&& _start + _waitingEntity->offset() + _waitingEntity->length() <= _ptr) {
		++_waitingEntity;
	}
}

void Parser::skipBadEntities() {
	if (_links.size() >= 0x7FFF) {
		while (_waitingEntity != _entitiesEnd
			&& (isLinkEntity(*_waitingEntity)
				|| isInvalidEntity(*_waitingEntity))) {
			++_waitingEntity;
		}
	} else {
		while (_waitingEntity != _entitiesEnd && isInvalidEntity(*_waitingEntity)) {
			++_waitingEntity;
		}
	}
}

void Parser::parseCurrentChar() {
	_ch = ((_ptr < _end) ? *_ptr : 0);
	_emojiLookback = 0;
	const auto inCustomEmoji = !_customEmojiData.isEmpty();
	const auto isNewLine = !inCustomEmoji && _multiline && IsNewline(_ch);
	const auto isSpace = IsSpace(_ch);
	const auto isDiac = IsDiac(_ch);
	const auto isTilde = !inCustomEmoji && _checkTilde && (_ch == '~');
	const auto skip = [&] {
		if (IsBad(_ch) || _ch.isLowSurrogate()) {
			return true;
		} else if (_ch == 0xFE0F && Platform::IsMac()) {
			// Some sequences like 0x0E53 0xFE0F crash OS X harfbuzz text processing :(
			return true;
		} else if (isDiac) {
			if (_lastSkipped || _emoji || ++_diacs > kMaxDiacAfterSymbol) {
				return true;
			}
		} else if (_ch.isHighSurrogate()) {
			if (_ptr + 1 >= _end || !(_ptr + 1)->isLowSurrogate()) {
				return true;
			}
			const auto ucs4 = QChar::surrogateToUcs4(_ch, *(_ptr + 1));
			if (ucs4 >= 0xE0000) {
				// Unicode tags are skipped.
				// Only place they work is in some flag emoji,
				// but in that case they were already parsed as emoji before.
				//
				// For unknown reason in some unknown cases strings with such
				// symbols lead to crashes on some Linux distributions, see
				// https://github.com/telegramdesktop/tdesktop/issues/7005
				//
				// At least one crashing text was starting that way:
				//
				// 0xd83d 0xdcda 0xdb40 0xdc69 0xdb40 0xdc64 0xdb40 0xdc6a
				// 0xdb40 0xdc77 0xdb40 0xdc7f 0x32 ... simple text here ...
				//
				// or in codepoints:
				//
				// 0x1f4da 0xe0069 0xe0064 0xe006a 0xe0077 0xe007f 0x32 ...
				return true;
			}
		}
		return false;
	}();

	if (_ch.isHighSurrogate() && !skip) {
		_t->_text.push_back(_ch);
		++_ptr;
		_ch = *_ptr;
		_emojiLookback = 1;
	}

	_lastSkipped = skip;
	if (skip) {
		_ch = 0;
	} else {
		if (isTilde) { // tilde fix in OpenSans
			if (!(_flags & TextBlockFTilde)) {
				createBlock(-_emojiLookback);
				_flags |= TextBlockFTilde;
			}
		} else {
			if (_flags & TextBlockFTilde) {
				createBlock(-_emojiLookback);
				_flags &= ~TextBlockFTilde;
			}
		}
		if (isNewLine) {
			createNewlineBlock();
		} else if (isSpace) {
			_t->_text.push_back(QChar::Space);
		} else {
			if (_emoji) {
				createBlock(-_emojiLookback);
			}
			_t->_text.push_back(_ch);
		}
		if (!isDiac) _diacs = 0;
	}
}

void Parser::parseEmojiFromCurrent() {
	if (!_customEmojiData.isEmpty()) {
		return;
	}
	int len = 0;
	auto e = Emoji::Find(_ptr - _emojiLookback, _end, &len);
	if (!e) return;

	for (int l = len - _emojiLookback - 1; l > 0; --l) {
		_t->_text.push_back(*++_ptr);
	}
	if (e->hasPostfix()) {
		Assert(!_t->_text.isEmpty());
		const auto last = _t->_text[_t->_text.size() - 1];
		if (last.unicode() != Emoji::kPostfix) {
			_t->_text.push_back(QChar(Emoji::kPostfix));
			++len;
		}
	}

	createBlock(-len);
	_emoji = e;
}

bool Parser::isInvalidEntity(const EntityInText &entity) const {
	const auto length = entity.length();
	return (_start + entity.offset() + length > _end) || (length <= 0);
}

bool Parser::isLinkEntity(const EntityInText &entity) const {
	const auto type = entity.type();
	const auto urls = {
		EntityType::Url,
		EntityType::CustomUrl,
		EntityType::Email,
		EntityType::Hashtag,
		EntityType::Cashtag,
		EntityType::Mention,
		EntityType::MentionName,
		EntityType::BotCommand
	};
	return ranges::find(urls, type) != std::end(urls);
}

void Parser::parse(const TextParseOptions &options) {
	skipBadEntities();
	trimSourceRange();

	_t->_text.resize(0);
	_t->_text.reserve(_end - _ptr);

	for (; _ptr <= _end; ++_ptr) {
		while (checkEntities()) {
		}
		parseCurrentChar();
		parseEmojiFromCurrent();

		if (_sumFinished || _t->_text.size() >= 0x8000) {
			break; // 32k max
		}
	}
	createBlock();
	finalize(options);
}

void Parser::trimSourceRange() {
	const auto firstMonospaceOffset = EntityInText::FirstMonospaceOffset(
		_source.entities,
		_end - _start);

	while (_ptr != _end && IsTrimmed(*_ptr) && _ptr != _start + firstMonospaceOffset) {
		++_ptr;
	}
	while (_ptr != _end && IsTrimmed(*(_end - 1))) {
		--_end;
	}
}

// void Parser::checkForElidedSkipBlock() {
// 	if (!_sumFinished || !_rich) {
// 		return;
// 	}
// 	// We could've skipped the final skip block command.
// 	for (; _ptr < _end; ++_ptr) {
// 		if (*_ptr == TextCommand && readSkipBlockCommand()) {
// 			break;
// 		}
// 	}
// }

void Parser::finalize(const TextParseOptions &options) {
	_t->_links.resize(_maxLnkIndex + _maxShiftedLnkIndex);
	auto counterCustomIndex = uint16(0);
	auto currentIndex = uint16(0); // Current the latest index of _t->_links.
	struct {
		uint16 mono = 0;
		uint16 lnk = 0;
	} lastHandlerIndex;
	const auto avoidIntersectionsWithCustom = [&] {
		while (ranges::contains(_linksIndexes, currentIndex)) {
			currentIndex++;
		}
	};
	_t->_hasCustomEmoji = false;
	for (auto &block : _t->_blocks) {
		if (block->type() == TextBlockTCustomEmoji) {
			_t->_hasCustomEmoji = true;
		}
		const auto spoilerIndex = block->spoilerIndex();
		if (spoilerIndex && (_t->_spoilers.size() < spoilerIndex)) {
			_t->_spoilers.resize(spoilerIndex);
			const auto handler = (options.flags & TextParseLinks)
				? std::make_shared<SpoilerClickHandler>()
				: nullptr;
			_t->setSpoiler(spoilerIndex, std::move(handler));
		}
		const auto shiftedIndex = block->lnkIndex();
		auto useCustomIndex = false;
		if (shiftedIndex <= kStringLinkIndexShift) {
			if (IsMono(block->flags()) && shiftedIndex) {
				const auto monoIndex = shiftedIndex;

				if (lastHandlerIndex.mono == monoIndex) {
					block->setLnkIndex(currentIndex);
					continue; // Optimization.
				} else {
					currentIndex++;
				}
				avoidIntersectionsWithCustom();
				block->setLnkIndex(currentIndex);
				const auto handler = Integration::Instance().createLinkHandler(
					_monos[monoIndex - 1],
					_context);
				_t->_links.resize(currentIndex);
				if (handler) {
					_t->setLink(currentIndex, handler);
				}
				lastHandlerIndex.mono = monoIndex;
				continue;
			} else if (shiftedIndex) {
				useCustomIndex = true;
			} else {
				continue;
			}
		}
		const auto usedIndex = [&] {
			return useCustomIndex
				? _linksIndexes[counterCustomIndex - 1]
				: currentIndex;
		};
		const auto realIndex = useCustomIndex
			? shiftedIndex
			: (shiftedIndex - kStringLinkIndexShift);
		if (lastHandlerIndex.lnk == realIndex) {
			block->setLnkIndex(usedIndex());
			continue; // Optimization.
		} else {
			(useCustomIndex ? counterCustomIndex : currentIndex)++;
		}
		if (!useCustomIndex) {
			avoidIntersectionsWithCustom();
		}
		block->setLnkIndex(usedIndex());

		_t->_links.resize(std::max(usedIndex(), uint16(_t->_links.size())));
		const auto handler = Integration::Instance().createLinkHandler(
			_links[realIndex - 1],
			_context);
		if (handler) {
			_t->setLink(usedIndex(), handler);
		}
		lastHandlerIndex.lnk = realIndex;
	}
	_t->_links.squeeze();
	_t->_spoilers.squeeze();
	_t->_blocks.shrink_to_fit();
	_t->_text.squeeze();
}

void Parser::computeLinkText(
		const QString &linkData,
		QString *outLinkText,
		EntityLinkShown *outShown) {
	auto url = QUrl(linkData);
	auto good = QUrl(url.isValid()
		? url.toEncoded()
		: QByteArray());
	auto readable = good.isValid()
		? good.toDisplayString()
		: linkData;
	*outLinkText = _t->_st->font->elided(readable, st::linkCropLimit);
	*outShown = (*outLinkText == readable)
		? EntityLinkShown::Full
		: EntityLinkShown::Partial;
}

namespace {

// COPIED FROM qtextengine.cpp AND MODIFIED

struct BidiStatus {
	BidiStatus() {
		eor = QChar::DirON;
		lastStrong = QChar::DirON;
		last = QChar:: DirON;
		dir = QChar::DirON;
	}
	QChar::Direction eor;
	QChar::Direction lastStrong;
	QChar::Direction last;
	QChar::Direction dir;
};

enum { _MaxBidiLevel = 61 };
enum { _MaxItemLength = 4096 };

struct BidiControl {
	inline BidiControl(bool rtl)
		: base(rtl ? 1 : 0), level(rtl ? 1 : 0) {}

	inline void embed(bool rtl, bool o = false) {
		unsigned int toAdd = 1;
		if((level%2 != 0) == rtl ) {
			++toAdd;
		}
		if (level + toAdd <= _MaxBidiLevel) {
			ctx[cCtx].level = level;
			ctx[cCtx].override = override;
			cCtx++;
			override = o;
			level += toAdd;
		}
	}
	inline bool canPop() const { return cCtx != 0; }
	inline void pdf() {
		Q_ASSERT(cCtx);
		--cCtx;
		level = ctx[cCtx].level;
		override = ctx[cCtx].override;
	}

	inline QChar::Direction basicDirection() const {
		return (base ? QChar::DirR : QChar:: DirL);
	}
	inline unsigned int baseLevel() const {
		return base;
	}
	inline QChar::Direction direction() const {
		return ((level%2) ? QChar::DirR : QChar:: DirL);
	}

	struct {
		unsigned int level = 0;
		bool override = false;
	} ctx[_MaxBidiLevel];
	unsigned int cCtx = 0;
	const unsigned int base;
	unsigned int level;
	bool override = false;
};

static void eAppendItems(QScriptAnalysis *analysis, int &start, int &stop, const BidiControl &control, QChar::Direction dir) {
	if (start > stop)
		return;

	int level = control.level;

	if(dir != QChar::DirON && !control.override) {
		// add level of run (cases I1 & I2)
		if(level % 2) {
			if(dir == QChar::DirL || dir == QChar::DirAN || dir == QChar::DirEN)
				level++;
		} else {
			if(dir == QChar::DirR)
				level++;
			else if(dir == QChar::DirAN || dir == QChar::DirEN)
				level += 2;
		}
	}

	QScriptAnalysis *s = analysis + start;
	const QScriptAnalysis *e = analysis + stop;
	while (s <= e) {
		s->bidiLevel = level;
		++s;
	}
	++stop;
	start = stop;
}

inline int32 countBlockHeight(const AbstractBlock *b, const style::TextStyle *st) {
	return (b->type() == TextBlockTSkip) ? static_cast<const SkipBlock*>(b)->height() : (st->lineHeight > st->font->height) ? st->lineHeight : st->font->height;
}

} // namespace

class Renderer {
public:
	Renderer(Painter *p, const String *t)
	: _p(p)
	, _t(t)
	, _originalPen(p ? p->pen() : QPen()) {
	}

	~Renderer() {
		restoreAfterElided();
		if (_p) {
			_p->setPen(_originalPen);
		}
	}

	void draw(int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, TextSelection selection = { 0, 0 }, bool fullWidthSelection = true) {
		if (_t->isEmpty()) return;

		_blocksSize = _t->_blocks.size();
		if (_p) {
			_p->setFont(_t->_st->font);
			_textPalette = &_p->textPalette();
			_originalPenSelected = (_textPalette->selectFg->c.alphaF() == 0) ? _originalPen : _textPalette->selectFg->p;
		}

		_x = left;
		_y = top;
		_yFrom = yFrom + top;
		_yTo = (yTo < 0) ? -1 : (yTo + top);
		_selection = selection;
		_fullWidthSelection = fullWidthSelection;
		_wLeft = _w = w;
		if (_elideLast) {
			_yToElide = _yTo;
			if (_elideRemoveFromEnd > 0 && !_t->_blocks.empty()) {
				int firstBlockHeight = countBlockHeight(_t->_blocks.front().get(), _t->_st);
				if (_y + firstBlockHeight >= _yToElide) {
					_wLeft -= _elideRemoveFromEnd;
				}
			}
		}
		_str = _t->_text.unicode();

		if (_p) {
			auto clip = _p->hasClipping() ? _p->clipBoundingRect() : QRect();
			if (clip.width() > 0 || clip.height() > 0) {
				if (_yFrom < clip.y()) _yFrom = clip.y();
				if (_yTo < 0 || _yTo > clip.y() + clip.height()) _yTo = clip.y() + clip.height();
			}
		}

		_align = align;

		_parDirection = _t->_startDir;
		if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = style::LayoutDirection();
		if ((*_t->_blocks.cbegin())->type() != TextBlockTNewline) {
			initNextParagraph(_t->_blocks.cbegin());
		}

		_lineStart = 0;
		_lineStartBlock = 0;

		_lineHeight = 0;
		_fontHeight = _t->_st->font->height;
		auto last_rBearing = QFixed(0);
		_last_rPadding = QFixed(0);

		auto blockIndex = 0;
		bool longWordLine = true;
		auto e = _t->_blocks.cend();
		for (auto i = _t->_blocks.cbegin(); i != e; ++i, ++blockIndex) {
			auto b = i->get();
			auto _btype = b->type();
			auto blockHeight = countBlockHeight(b, _t->_st);

			if (_btype == TextBlockTNewline) {
				if (!_lineHeight) _lineHeight = blockHeight;
				if (!drawLine((*i)->from(), i, e)) {
					return;
				}

				_y += _lineHeight;
				_lineHeight = 0;
				_lineStart = _t->countBlockEnd(i, e);
				_lineStartBlock = blockIndex + 1;

				last_rBearing = b->f_rbearing();
				_last_rPadding = b->f_rpadding();
				_wLeft = _w - (b->f_width() - last_rBearing);
				if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
					_wLeft -= _elideRemoveFromEnd;
				}

				_parDirection = static_cast<const NewlineBlock*>(b)->nextDirection();
				if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = style::LayoutDirection();
				initNextParagraph(i + 1);

				longWordLine = true;
				continue;
			}

			auto b__f_rbearing = b->f_rbearing();
			auto newWidthLeft = _wLeft - last_rBearing - (_last_rPadding + b->f_width() - b__f_rbearing);
			if (newWidthLeft >= 0) {
				last_rBearing = b__f_rbearing;
				_last_rPadding = b->f_rpadding();
				_wLeft = newWidthLeft;

				_lineHeight = qMax(_lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			if (_btype == TextBlockTText) {
				auto t = static_cast<const TextBlock*>(b);
				if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
					_last_rPadding += b->f_rpadding();

					_lineHeight = qMax(_lineHeight, blockHeight);

					longWordLine = false;
					continue;
				}

				auto f_wLeft = _wLeft; // vars for saving state of the last word start
				auto f_lineHeight = _lineHeight; // f points to the last word-start element of t->_words
				for (auto j = t->_words.cbegin(), en = t->_words.cend(), f = j; j != en; ++j) {
					auto wordEndsHere = (j->f_width() >= 0);
					auto j_width = wordEndsHere ? j->f_width() : -j->f_width();

					auto newWidthLeft = _wLeft - last_rBearing - (_last_rPadding + j_width - j->f_rbearing());
					if (newWidthLeft >= 0) {
						last_rBearing = j->f_rbearing();
						_last_rPadding = j->f_rpadding();
						_wLeft = newWidthLeft;

						_lineHeight = qMax(_lineHeight, blockHeight);

						if (wordEndsHere) {
							longWordLine = false;
						}
						if (wordEndsHere || longWordLine) {
							f = j + 1;
							f_wLeft = _wLeft;
							f_lineHeight = _lineHeight;
						}
						continue;
					}

					auto elidedLineHeight = qMax(_lineHeight, blockHeight);
					auto elidedLine = _elideLast && (_y + elidedLineHeight >= _yToElide);
					if (elidedLine) {
						_lineHeight = elidedLineHeight;
					} else if (f != j && !_breakEverywhere) {
						// word did not fit completely, so we roll back the state to the beginning of this long word
						j = f;
						_wLeft = f_wLeft;
						_lineHeight = f_lineHeight;
						j_width = (j->f_width() >= 0) ? j->f_width() : -j->f_width();
					}
					if (!drawLine(elidedLine ? ((j + 1 == en) ? _t->countBlockEnd(i, e) : (j + 1)->from()) : j->from(), i, e)) {
						return;
					}
					_y += _lineHeight;
					_lineHeight = qMax(0, blockHeight);
					_lineStart = j->from();
					_lineStartBlock = blockIndex;

					last_rBearing = j->f_rbearing();
					_last_rPadding = j->f_rpadding();
					_wLeft = _w - (j_width - last_rBearing);
					if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
						_wLeft -= _elideRemoveFromEnd;
					}

					longWordLine = !wordEndsHere;
					f = j + 1;
					f_wLeft = _wLeft;
					f_lineHeight = _lineHeight;
				}
				continue;
			}

			auto elidedLineHeight = qMax(_lineHeight, blockHeight);
			auto elidedLine = _elideLast && (_y + elidedLineHeight >= _yToElide);
			if (elidedLine) {
				_lineHeight = elidedLineHeight;
			}
			if (!drawLine(elidedLine ? _t->countBlockEnd(i, e) : b->from(), i, e)) {
				return;
			}
			_y += _lineHeight;
			_lineHeight = qMax(0, blockHeight);
			_lineStart = b->from();
			_lineStartBlock = blockIndex;

			last_rBearing = b__f_rbearing;
			_last_rPadding = b->f_rpadding();
			_wLeft = _w - (b->f_width() - last_rBearing);
			if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
				_wLeft -= _elideRemoveFromEnd;
			}

			longWordLine = true;
			continue;
		}
		if (_lineStart < _t->_text.size()) {
			if (!drawLine(_t->_text.size(), e, e)) return;
		}
		if (!_p && _lookupSymbol) {
			_lookupResult.symbol = _t->_text.size();
			_lookupResult.afterSymbol = false;
		}
	}

	void drawElided(int32 left, int32 top, int32 w, style::align align, int32 lines, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) {
		if (lines <= 0 || _t->isNull()) return;

		if (yTo < 0 || (lines - 1) * _t->_st->font->height < yTo) {
			yTo = lines * _t->_st->font->height;
			_elideLast = true;
			_elideRemoveFromEnd = removeFromEnd;
		}
		_breakEverywhere = breakEverywhere;
		draw(left, top, w, align, yFrom, yTo, selection);
	}

	StateResult getState(QPoint point, int w, StateRequest request) {
		if (!_t->isNull() && point.y() >= 0) {
			_lookupRequest = request;
			_lookupX = point.x();
			_lookupY = point.y();

			_breakEverywhere = (_lookupRequest.flags & StateRequest::Flag::BreakEverywhere);
			_lookupSymbol = (_lookupRequest.flags & StateRequest::Flag::LookupSymbol);
			_lookupLink = (_lookupRequest.flags & StateRequest::Flag::LookupLink);
			if (_lookupSymbol || (_lookupX >= 0 && _lookupX < w)) {
				draw(0, 0, w, _lookupRequest.align, _lookupY, _lookupY + 1);
			}
		}
		return _lookupResult;
	}

	StateResult getStateElided(QPoint point, int w, StateRequestElided request) {
		if (!_t->isNull() && point.y() >= 0 && request.lines > 0) {
			_lookupRequest = request;
			_lookupX = point.x();
			_lookupY = point.y();

			_breakEverywhere = (_lookupRequest.flags & StateRequest::Flag::BreakEverywhere);
			_lookupSymbol = (_lookupRequest.flags & StateRequest::Flag::LookupSymbol);
			_lookupLink = (_lookupRequest.flags & StateRequest::Flag::LookupLink);
			if (_lookupSymbol || (_lookupX >= 0 && _lookupX < w)) {
				int yTo = _lookupY + 1;
				if (yTo < 0 || (request.lines - 1) * _t->_st->font->height < yTo) {
					yTo = request.lines * _t->_st->font->height;
					_elideLast = true;
					_elideRemoveFromEnd = request.removeFromEnd;
				}
				draw(0, 0, w, _lookupRequest.align, _lookupY, _lookupY + 1);
			}
		}
		return _lookupResult;
	}

private:
	void initNextParagraph(String::TextBlocks::const_iterator i) {
		_parStartBlock = i;
		const auto e = _t->_blocks.cend();
		if (i == e) {
			_parStart = _t->_text.size();
			_parLength = 0;
		} else {
			_parStart = (*i)->from();
			for (; i != e; ++i) {
				if ((*i)->type() == TextBlockTNewline) {
					break;
				}
			}
			_parLength = ((i == e) ? _t->_text.size() : (*i)->from()) - _parStart;
		}
		_parAnalysis.resize(0);
	}

	void initParagraphBidi() {
		if (!_parLength || !_parAnalysis.isEmpty()) return;

		String::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		bool ignore = false;
		bool rtl = (_parDirection == Qt::RightToLeft);
		if (!ignore && !rtl) {
			ignore = true;
			const ushort *start = reinterpret_cast<const ushort*>(_str) + _parStart;
			const ushort *curr = start;
			const ushort *end = start + _parLength;
			while (curr < end) {
				while (n != e && (*n)->from() <= _parStart + (curr - start)) {
					i = n;
					++n;
				}
				const auto type = (*i)->type();
				if (type != TextBlockTEmoji
					&& type != TextBlockTCustomEmoji
					&& *curr >= 0x590) {
					ignore = false;
					break;
				}
				++curr;
			}
		}

		_parAnalysis.resize(_parLength);
		QScriptAnalysis *analysis = _parAnalysis.data();

		BidiControl control(rtl);

		_parHasBidi = false;
		if (ignore) {
			memset(analysis, 0, _parLength * sizeof(QScriptAnalysis));
			if (rtl) {
				for (int i = 0; i < _parLength; ++i)
					analysis[i].bidiLevel = 1;
				_parHasBidi = true;
			}
		} else {
			_parHasBidi = eBidiItemize(analysis, control);
		}
	}

	bool drawLine(uint16 _lineEnd, const String::TextBlocks::const_iterator &_endBlockIter, const String::TextBlocks::const_iterator &_end) {
		_yDelta = (_lineHeight - _fontHeight) / 2;
		if (_yTo >= 0 && (_y + _yDelta >= _yTo || _y >= _yTo)) return false;
		if (_y + _yDelta + _fontHeight <= _yFrom) {
			if (_lookupSymbol) {
				_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
				_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
			}
			return true;
		}

		// Trimming pending spaces, because they sometimes don't fit on the line.
		// They also are not counted in the line width, they're in the right padding.
		// Line width is a sum of block / word widths and paddings between them, without trailing one.
		auto trimmedLineEnd = _lineEnd;
		for (; trimmedLineEnd > _lineStart; --trimmedLineEnd) {
			auto ch = _t->_text[trimmedLineEnd - 1];
			if (ch != QChar::Space && ch != QChar::LineFeed) {
				break;
			}
		}

		auto _endBlock = (_endBlockIter == _end) ? nullptr : _endBlockIter->get();
		auto elidedLine = _elideLast && (_y + _lineHeight >= _yToElide);
		if (elidedLine) {
			// If we decided to draw the last line elided only because of the skip block
			// that did not fit on this line, we just draw the line till the very end.
			// Skip block is ignored in the elided lines, instead "removeFromEnd" is used.
			if (_endBlock && _endBlock->type() == TextBlockTSkip) {
				_endBlock = nullptr;
			}
			if (!_endBlock) {
				elidedLine = false;
			}
		}

		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		const auto extendLeft = (currentBlock->from() < _lineStart)
			? qMin(_lineStart - currentBlock->from(), 2)
			: 0;
		_localFrom = _lineStart - extendLeft;
		const auto extendedLineEnd = (_endBlock && _endBlock->from() < trimmedLineEnd && !elidedLine)
			? qMin(uint16(trimmedLineEnd + 2), _t->countBlockEnd(_endBlockIter, _end))
			: trimmedLineEnd;

		auto lineText = _t->_text.mid(_localFrom, extendedLineEnd - _localFrom);
		auto lineStart = extendLeft;
		auto lineLength = trimmedLineEnd - _lineStart;

		if (elidedLine) {
			initParagraphBidi();
			prepareElidedLine(lineText, lineStart, lineLength, _endBlock);
		}

		auto x = _x;
		if (_align & Qt::AlignHCenter) {
			x += (_wLeft / 2).toInt();
		} else if (((_align & Qt::AlignLeft) && _parDirection == Qt::RightToLeft) || ((_align & Qt::AlignRight) && _parDirection == Qt::LeftToRight)) {
			x += _wLeft;
		}

		if (!_p) {
			if (_lookupX < x) {
				if (_lookupSymbol) {
					if (_parDirection == Qt::RightToLeft) {
						_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
						_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
//						_lookupResult.uponSymbol = ((_lookupX >= _x) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockTSkip)) ? true : false;
					} else {
						_lookupResult.symbol = _lineStart;
						_lookupResult.afterSymbol = false;
//						_lookupResult.uponSymbol = ((_lookupX >= _x) && (_lineStart > 0)) ? true : false;
					}
				}
				if (_lookupLink) {
					_lookupResult.link = nullptr;
				}
				_lookupResult.uponSymbol = false;
				return false;
			} else if (_lookupX >= x + (_w - _wLeft)) {
				if (_parDirection == Qt::RightToLeft) {
					_lookupResult.symbol = _lineStart;
					_lookupResult.afterSymbol = false;
//					_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (_lineStart > 0)) ? true : false;
				} else {
					_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
					_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
//					_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockTSkip)) ? true : false;
				}
				if (_lookupLink) {
					_lookupResult.link = nullptr;
				}
				_lookupResult.uponSymbol = false;
				return false;
			}
		}

		if (_fullWidthSelection) {
			const auto selectFromStart = (_selection.to > _lineStart)
				&& (_lineStart > 0)
				&& (_selection.from <= _lineStart);
			const auto selectTillEnd = (_selection.to > trimmedLineEnd)
				&& (trimmedLineEnd < _t->_text.size())
				&& (_selection.from <= trimmedLineEnd)
				&& (!_endBlock || _endBlock->type() != TextBlockTSkip);

			if ((selectFromStart && _parDirection == Qt::LeftToRight)
				|| (selectTillEnd && _parDirection == Qt::RightToLeft)) {
				if (x > _x) {
					fillSelectRange(_x, x);
				}
			}
			if ((selectTillEnd && _parDirection == Qt::LeftToRight)
				|| (selectFromStart && _parDirection == Qt::RightToLeft)) {
				if (x < _x + _wLeft) {
					fillSelectRange(x + _w - _wLeft, _x + _w);
				}
			}
		}
		if (trimmedLineEnd == _lineStart && !elidedLine) {
			return true;
		}

		if (!elidedLine) initParagraphBidi(); // if was not inited

		_f = _t->_st->font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
		int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0;
		if (!nItems) {
			return true;
		}

		int skipIndex = -1;
		QVarLengthArray<int> visualOrder(nItems);
		QVarLengthArray<uchar> levels(nItems);
		for (int i = 0; i < nItems; ++i) {
			auto &si = engine.layoutData->items[firstItem + i];
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			}
			auto _type = currentBlock->type();
			if (_type == TextBlockTSkip) {
				levels[i] = si.analysis.bidiLevel = 0;
				skipIndex = i;
			} else {
				levels[i] = si.analysis.bidiLevel;
			}
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockTEmoji
					|| _type == TextBlockTCustomEmoji
					|| _type == TextBlockTSkip) {
					si.width = currentBlock->f_width()
						+ (nextBlock == _endBlock && (!nextBlock || nextBlock->from() >= trimmedLineEnd)
							? 0
							: currentBlock->f_rpadding());
				}
			}
		}
		QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());
		if (style::RightToLeft() && skipIndex == nItems - 1) {
			for (int32 i = nItems; i > 1;) {
				--i;
				visualOrder[i] = visualOrder[i - 1];
			}
			visualOrder[0] = skipIndex;
		}

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex].get();
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		int32 textY = _y + _yDelta + _t->_st->font->ascent, emojiY = (_t->_st->font->height - st::emojiSize) / 2;

		applyBlockProperties(currentBlock);
		for (int i = 0; i < nItems; ++i) {
			int item = firstItem + visualOrder[i];
			const QScriptItem &si = engine.layoutData->items.at(item);
			bool rtl = (si.analysis.bidiLevel % 2);

			while (blockIndex > _lineStartBlock + 1 && _t->_blocks[blockIndex - 1]->from() > _localFrom + si.position) {
				nextBlock = currentBlock;
				currentBlock = _t->_blocks[--blockIndex - 1].get();
				applyBlockProperties(currentBlock);
			}
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
				applyBlockProperties(currentBlock);
			}
			if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
				TextBlockType _type = currentBlock->type();
				if (!_p && _lookupX >= x && _lookupX < x + si.width) { // _lookupRequest
					if (_lookupLink) {
						if (_lookupY >= _y + _yDelta && _lookupY < _y + _yDelta + _fontHeight) {
							const auto spoilerLink = _t->spoilerLink(currentBlock->spoilerIndex());
							const auto resultLink = (spoilerLink || !currentBlock->lnkIndex())
								? spoilerLink
								: _t->_links.at(currentBlock->lnkIndex() - 1);
							if (resultLink) {
								_lookupResult.link = resultLink;
							}
						}
					}
					if (_type != TextBlockTSkip) {
						_lookupResult.uponSymbol = true;
					}
					if (_lookupSymbol) {
						if (_type == TextBlockTSkip) {
							if (_parDirection == Qt::RightToLeft) {
								_lookupResult.symbol = _lineStart;
								_lookupResult.afterSymbol = false;
							} else {
								_lookupResult.symbol = (trimmedLineEnd > _lineStart) ? (trimmedLineEnd - 1) : _lineStart;
								_lookupResult.afterSymbol = (trimmedLineEnd > _lineStart) ? true : false;
							}
							return false;
						}

						// Emoji with spaces after symbol lookup
						auto chFrom = _str + currentBlock->from();
						auto chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
						auto spacesWidth = (si.width - currentBlock->f_width());
						auto spacesCount = 0;
						while (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space) {
							++spacesCount;
							--chTo;
						}
						if (spacesCount > 0) { // Check if we're over a space.
							if (rtl) {
								if (_lookupX < x + spacesWidth) {
									_lookupResult.symbol = (chTo - _str); // up to a space, included, rtl
									_lookupResult.afterSymbol = (_lookupX < x + (spacesWidth / 2)) ? true : false;
									return false;
								}
							} else if (_lookupX >= x + si.width - spacesWidth) {
								_lookupResult.symbol = (chTo - _str); // up to a space, inclided, ltr
								_lookupResult.afterSymbol = (_lookupX >= x + si.width - spacesWidth + (spacesWidth / 2)) ? true : false;
								return false;
							}
						}
						if (_lookupX < x + (rtl ? (si.width - currentBlock->f_width()) : 0) + (currentBlock->f_width() / 2)) {
							_lookupResult.symbol = ((rtl && chTo > chFrom) ? (chTo - 1) : chFrom) - _str;
							_lookupResult.afterSymbol = (rtl && chTo > chFrom) ? true : false;
						} else {
							_lookupResult.symbol = ((rtl || chTo <= chFrom) ? chFrom : (chTo - 1)) - _str;
							_lookupResult.afterSymbol = (rtl || chTo <= chFrom) ? false : true;
						}
					}
					return false;
				} else if (_p && (_type == TextBlockTEmoji || _type == TextBlockTCustomEmoji)) {
					auto glyphX = x;
					auto spacesWidth = (si.width - currentBlock->f_width());
					if (rtl) {
						glyphX += spacesWidth;
					}
					struct {
						QFixed from;
						QFixed to;
					} fillSelect;
					struct {
						QFixed from;
						QFixed width;
					} fillSpoiler;
					if (_background.selectActiveBlock) {
						fillSelect = { x, x + si.width };
					} else if (_localFrom + si.position < _selection.to) {
						auto chFrom = _str + currentBlock->from();
						auto chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
						if (_localFrom + si.position >= _selection.from) { // could be without space
							if (chTo == chFrom || (chTo - 1)->unicode() != QChar::Space || _selection.to >= (chTo - _str)) {
								fillSelect = { x, x + si.width };
							} else { // or with space
								fillSelect = { glyphX, glyphX + currentBlock->f_width() };
							}
						} else if (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space && (chTo - 1 - _str) >= _selection.from) {
							if (rtl) { // rtl space only
								fillSelect = { x, glyphX };
							} else { // ltr space only
								fillSelect = { x + currentBlock->f_width(), x + si.width };
							}
						}
					}
					const auto hasSpoiler = _background.color &&
						(_background.inFront || _background.startMs);
					if (hasSpoiler) {
						fillSpoiler = { x, si.width };
					}
					const auto spoilerOpacity = hasSpoiler
						? fillSpoilerOpacity()
						: 0.;
					const auto hasSelect = fillSelect.to != QFixed();
					if (hasSelect) {
						fillSelectRange(fillSelect.from, fillSelect.to);
					}
					const auto opacity = _p->opacity();
					if (spoilerOpacity < 1.) {
						if (hasSpoiler) {
							_p->setOpacity(opacity * (1. - spoilerOpacity));
						}
						const auto x = (glyphX + st::emojiPadding).toInt();
						const auto y = _y + _yDelta + emojiY;
						if (_type == TextBlockTEmoji) {
							Emoji::Draw(
								*_p,
								static_cast<const EmojiBlock*>(currentBlock)->_emoji,
								Emoji::GetSizeNormal(),
								x,
								y);
						} else if (const auto custom = static_cast<const CustomEmojiBlock*>(currentBlock)->_custom.get()) {
							if (!_now) {
								_now = crl::now();
							}
							if (!_customEmojiSize) {
								_customEmojiSize = AdjustCustomEmojiSize(st::emojiSize);
								_customEmojiSkip = (st::emojiSize - _customEmojiSize) / 2;
							}
							custom->paint(
								*_p,
								x + _customEmojiSkip,
								y + _customEmojiSkip,
								_now,
								_textPalette->spoilerActiveBg->c,
								_p->inactive());
						}
					}
					if (hasSpoiler) {
						_p->setOpacity(opacity * spoilerOpacity);
						fillSpoilerRange(
							fillSpoiler.from,
							fillSpoiler.width,
							blockIndex,
							currentBlock->from(),
							(nextBlock ? nextBlock->from() : _t->_text.size()));
						_p->setOpacity(opacity);
					}
//				} else if (_p && currentBlock->type() == TextBlockSkip) { // debug
//					_p->fillRect(QRect(x.toInt(), _y, currentBlock->width(), static_cast<SkipBlock*>(currentBlock)->height()), QColor(0, 0, 0, 32));
				}
				x += si.width;
				continue;
			}

			unsigned short *logClusters = engine.logClusters(&si);
			QGlyphLayout glyphs = engine.shapedGlyphs(&si);

			int itemStart = qMax(line.from, si.position), itemEnd;
			int itemLength = engine.length(item);
			int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
			if (line.from + line.length < si.position + itemLength) {
				itemEnd = line.from + line.length;
				glyphsEnd = logClusters[itemEnd - si.position];
			} else {
				itemEnd = si.position + itemLength;
				glyphsEnd = si.num_glyphs;
			}

			QFixed itemWidth = 0;
			for (int g = glyphsStart; g < glyphsEnd; ++g)
				itemWidth += glyphs.effectiveAdvance(g);

			if (!_p && _lookupX >= x && _lookupX < x + itemWidth) { // _lookupRequest
				if (_lookupLink) {
					if (_lookupY >= _y + _yDelta && _lookupY < _y + _yDelta + _fontHeight) {
						const auto spoilerLink = _t->spoilerLink(currentBlock->spoilerIndex());
						const auto resultLink = (spoilerLink || !currentBlock->lnkIndex())
							? spoilerLink
							: _t->_links.at(currentBlock->lnkIndex() - 1);
						if (resultLink) {
							_lookupResult.link = resultLink;
						}
					}
				}
				_lookupResult.uponSymbol = true;
				if (_lookupSymbol) {
					QFixed tmpx = rtl ? (x + itemWidth) : x;
					for (int ch = 0, g, itemL = itemEnd - itemStart; ch < itemL;) {
						g = logClusters[itemStart - si.position + ch];
						QFixed gwidth = glyphs.effectiveAdvance(g);
						// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
						int ch2 = ch + 1;
						while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
							++ch2;
						}
						for (int charsCount = (ch2 - ch); ch < ch2; ++ch) {
							QFixed shift1 = QFixed(2 * (charsCount - (ch2 - ch)) + 2) * gwidth / QFixed(2 * charsCount),
								shift2 = QFixed(2 * (charsCount - (ch2 - ch)) + 1) * gwidth / QFixed(2 * charsCount);
							if ((rtl && _lookupX >= tmpx - shift1) ||
								(!rtl && _lookupX < tmpx + shift1)) {
								_lookupResult.symbol = _localFrom + itemStart + ch;
								if ((rtl && _lookupX >= tmpx - shift2) ||
									(!rtl && _lookupX < tmpx + shift2)) {
									_lookupResult.afterSymbol = false;
								} else {
									_lookupResult.afterSymbol = true;
								}
								return false;
							}
						}
						if (rtl) {
							tmpx -= gwidth;
						} else {
							tmpx += gwidth;
						}
					}
					if (itemEnd > itemStart) {
						_lookupResult.symbol = _localFrom + itemEnd - 1;
						_lookupResult.afterSymbol = true;
					} else {
						_lookupResult.symbol = _localFrom + itemStart;
						_lookupResult.afterSymbol = false;
					}
				}
				return false;
			} else if (_p) {
				QTextItemInt gf;
				gf.glyphs = glyphs.mid(glyphsStart, glyphsEnd - glyphsStart);
				gf.f = &_e->fnt;
				gf.chars = engine.layoutData->string.unicode() + itemStart;
				gf.num_chars = itemEnd - itemStart;
				gf.fontEngine = engine.fontEngine(si);
				gf.logClusters = logClusters + itemStart - si.position;
				gf.width = itemWidth;
				gf.justified = false;
				InitTextItemWithScriptItem(gf, si);

				auto hasSelected = false;
				auto hasNotSelected = true;
				auto selectedRect = QRect();
				if (_background.selectActiveBlock) {
					fillSelectRange(x, x + itemWidth);
				} else if (_localFrom + itemStart < _selection.to && _localFrom + itemEnd > _selection.from) {
					hasSelected = true;
					auto selX = x;
					auto selWidth = itemWidth;
					if (_localFrom + itemStart >= _selection.from && _localFrom + itemEnd <= _selection.to) {
						hasNotSelected = false;
					} else {
						selWidth = 0;
						int itemL = itemEnd - itemStart;
						int selStart = _selection.from - (_localFrom + itemStart), selEnd = _selection.to - (_localFrom + itemStart);
						if (selStart < 0) selStart = 0;
						if (selEnd > itemL) selEnd = itemL;
						for (int ch = 0, g; ch < selEnd;) {
							g = logClusters[itemStart - si.position + ch];
							QFixed gwidth = glyphs.effectiveAdvance(g);
							// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
							int ch2 = ch + 1;
							while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
								++ch2;
							}
							if (ch2 <= selStart) {
								selX += gwidth;
							} else if (ch >= selStart && ch2 <= selEnd) {
								selWidth += gwidth;
							} else {
								int sStart = ch, sEnd = ch2;
								if (ch < selStart) {
									sStart = selStart;
									selX += QFixed(sStart - ch) * gwidth / QFixed(ch2 - ch);
								}
								if (ch2 >= selEnd) {
									sEnd = selEnd;
									selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
									break;
								}
								selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
							}
							ch = ch2;
						}
					}
					if (rtl) selX = x + itemWidth - (selX - x) - selWidth;
					selectedRect = QRect(selX.toInt(), _y + _yDelta, (selX + selWidth).toInt() - selX.toInt(), _fontHeight);
					fillSelectRange(selX, selX + selWidth);
				}
				const auto hasSpoiler = (_background.inFront || _background.startMs);
				const auto spoilerOpacity = hasSpoiler
					? fillSpoilerOpacity()
					: 0.;
				const auto opacity = _p->opacity();
				const auto isElidedBlock = (!rtl)
					&& (_indexOfElidedBlock == blockIndex);
				if ((spoilerOpacity < 1.) || isElidedBlock) {
					if (hasSpoiler && !isElidedBlock) {
						_p->setOpacity(opacity * (1. - spoilerOpacity));
					}
					if (Q_UNLIKELY(hasSelected)) {
						if (Q_UNLIKELY(hasNotSelected)) {
							// There is a bug in retina QPainter clipping stack.
							// You can see glitches in rendering in such text:
							// aA
							// Aa
							// Where selection is both 'A'-s.
							// I can't debug it right now, this is a workaround.
#ifdef Q_OS_MAC
							_p->save();
#endif // Q_OS_MAC
							const auto clippingEnabled = _p->hasClipping();
							const auto clippingRegion = _p->clipRegion();
							_p->setClipRect(selectedRect, Qt::IntersectClip);
							_p->setPen(*_currentPenSelected);
							_p->drawTextItem(QPointF(x.toReal(), textY), gf);
							const auto externalClipping = clippingEnabled
								? clippingRegion
								: QRegion(QRect(
									(_x - _w).toInt(),
									_y - _lineHeight,
									(_x + 2 * _w).toInt(),
									_y + 2 * _lineHeight));
							_p->setClipRegion(externalClipping - selectedRect);
							_p->setPen(*_currentPen);
							_p->drawTextItem(QPointF(x.toReal(), textY), gf);
#ifdef Q_OS_MAC
							_p->restore();
#else // Q_OS_MAC
							if (clippingEnabled) {
								_p->setClipRegion(clippingRegion);
							} else {
								_p->setClipping(false);
							}
#endif // Q_OS_MAC
						} else {
							_p->setPen(*_currentPenSelected);
							_p->drawTextItem(QPointF(x.toReal(), textY), gf);
						}
					} else {
						_p->setPen(*_currentPen);
						_p->drawTextItem(QPointF(x.toReal(), textY), gf);
					}
				}

				if (hasSpoiler) {
					_p->setOpacity(opacity * spoilerOpacity);
					fillSpoilerRange(
						x,
						itemWidth,
						blockIndex,
						_localFrom + itemStart,
						_localFrom + itemEnd);
					_p->setOpacity(opacity);
				}
			}

			x += itemWidth;
		}
		return true;
	}
	void fillSelectRange(QFixed from, QFixed to) {
		auto left = from.toInt();
		auto width = to.toInt() - left;
		_p->fillRect(left, _y + _yDelta, width, _fontHeight, _textPalette->selectBg);
	}

	float fillSpoilerOpacity() {
		if (!_background.startMs) {
			return 1.;
		}
		if (!_now) {
			_now = crl::now();
		}
		const auto progress = float64(_now - _background.startMs)
			/ st::fadeWrapDuration;
		if ((progress > 1.) && _background.spoilerIndex) {
			const auto link = _t->_spoilers.at(_background.spoilerIndex - 1);
			if (link) {
				link->setStartMs(0);
			}
		}
		return (1. - std::min(progress, 1.));
	}

	void fillSpoilerRange(
			QFixed x,
			QFixed width,
			int currentBlockIndex,
			int positionFrom,
			int positionTill) {
		if (!_background.color) {
			return;
		}
		const auto elideOffset = (_indexOfElidedBlock == currentBlockIndex)
			? (_elideRemoveFromEnd + _f->elidew)
			: 0;

		const auto parts = [&] {
			const auto blockIndex = currentBlockIndex - 1;
			const auto block = _t->_blocks[blockIndex].get();
			const auto nextBlock = (blockIndex + 1 < _t->_blocks.size())
				? _t->_blocks[blockIndex + 1].get()
				: nullptr;
			const auto blockEnd = nextBlock ? nextBlock->from() : _t->_text.size();
			const auto now = block->spoilerIndex();
			const auto was = (positionFrom > block->from())
				? now
				: (blockIndex > 0)
				? _t->_blocks[blockIndex - 1]->spoilerIndex()
				: 0;
			const auto will = elideOffset
				? 0
				: (positionTill < blockEnd)
				? now
				: nextBlock
				? nextBlock->spoilerIndex()
				: 0;
			return RectPart::None
				| ((now != was) ? (RectPart::FullLeft) : RectPart::None)
				| ((now != will) ? (RectPart::FullRight) : RectPart::None);
		}();
		const auto hasLeft = (parts & RectPart::Left) != 0;
		const auto hasRight = (parts & RectPart::Right) != 0;

		const auto &cache = _background.inFront
			? _t->_spoilerCache
			: _t->_spoilerShownCache;
		const auto cornerWidth = cache.corners[0].width()
			/ style::DevicePixelRatio();
		const auto useWidth = ((x + width).toInt() - x.toInt()) - elideOffset;
		const auto rect = QRect(
			x.toInt(),
			_y + _yDelta,
			std::max(
				useWidth,
				(hasRight ? cornerWidth : 0) + (hasLeft ? cornerWidth : 0)),
			_fontHeight);
		if (!rect.isValid()) {
			return;
		}

		if (parts != RectPart::None) {
			DrawRoundedRect(
				*_p,
				rect,
				*_background.color,
				cache.corners,
				parts);
		}
		_p->fillRect(
			rect.left() + (hasLeft ? cornerWidth : 0),
			rect.top(),
			rect.width()
				- (hasRight ? cornerWidth : 0)
				- (hasLeft ? cornerWidth : 0),
			rect.height(),
			*_background.color);
	}

	void elideSaveBlock(int32 blockIndex, const AbstractBlock *&_endBlock, int32 elideStart, int32 elideWidth) {
		if (_elideSavedBlock) {
			restoreAfterElided();
		}

		_elideSavedIndex = blockIndex;
		auto mutableText = const_cast<String*>(_t);
		_elideSavedBlock = std::move(mutableText->_blocks[blockIndex]);
		mutableText->_blocks[blockIndex] = Block::Text(_t->_st->font, _t->_text, QFIXED_MAX, elideStart, 0, (*_elideSavedBlock)->flags(), (*_elideSavedBlock)->lnkIndex(), (*_elideSavedBlock)->spoilerIndex());
		_blocksSize = blockIndex + 1;
		_endBlock = (blockIndex + 1 < _t->_blocks.size() ? _t->_blocks[blockIndex + 1].get() : nullptr);
	}

	void setElideBidi(int32 elideStart, int32 elideLen) {
		int32 newParLength = elideStart + elideLen - _parStart;
		if (newParLength > _parAnalysis.size()) {
			_parAnalysis.resize(newParLength);
		}
		for (int32 i = elideLen; i > 0; --i) {
			_parAnalysis[newParLength - i].bidiLevel = (_parDirection == Qt::RightToLeft) ? 1 : 0;
		}
	}

	void prepareElidedLine(QString &lineText, int32 lineStart, int32 &lineLength, const AbstractBlock *&_endBlock, int repeat = 0) {
		_f = _t->_st->font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		auto elideWidth = _f->elidew;
		_wLeft = _w - elideWidth - _elideRemoveFromEnd;

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
		int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0, i;

		for (i = 0; i < nItems; ++i) {
			QScriptItem &si(engine.layoutData->items[firstItem + i]);
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			}
			TextBlockType _type = currentBlock->type();
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockTEmoji
					|| _type == TextBlockTCustomEmoji
					|| _type == TextBlockTSkip) {
					si.width = currentBlock->f_width() + currentBlock->f_rpadding();
				}
			}
			if (_type == TextBlockTEmoji
				|| _type == TextBlockTCustomEmoji
				|| _type == TextBlockTSkip
				|| _type == TextBlockTNewline) {
				if (_wLeft < si.width) {
					lineText = lineText.mid(0, currentBlock->from() - _localFrom) + kQEllipsis;
					lineLength = currentBlock->from() + kQEllipsis.size() - _lineStart;
					_selection.to = qMin(_selection.to, currentBlock->from());
					_indexOfElidedBlock = blockIndex + (nextBlock ? 1 : 0);
					setElideBidi(currentBlock->from(), kQEllipsis.size());
					elideSaveBlock(blockIndex - 1, _endBlock, currentBlock->from(), elideWidth);
					return;
				}
				_wLeft -= si.width;
			} else if (_type == TextBlockTText) {
				unsigned short *logClusters = engine.logClusters(&si);
				QGlyphLayout glyphs = engine.shapedGlyphs(&si);

				int itemStart = qMax(line.from, si.position), itemEnd;
				int itemLength = engine.length(firstItem + i);
				int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
				if (line.from + line.length < si.position + itemLength) {
					itemEnd = line.from + line.length;
					glyphsEnd = logClusters[itemEnd - si.position];
				} else {
					itemEnd = si.position + itemLength;
					glyphsEnd = si.num_glyphs;
				}

				for (auto g = glyphsStart; g < glyphsEnd; ++g) {
					auto adv = glyphs.effectiveAdvance(g);
					if (_wLeft < adv) {
						auto pos = itemStart;
						while (pos < itemEnd && logClusters[pos - si.position] < g) {
							++pos;
						}

						if (lineText.size() <= pos || repeat > 3) {
							lineText += kQEllipsis;
							lineLength = _localFrom + pos + kQEllipsis.size() - _lineStart;
							_selection.to = qMin(_selection.to, uint16(_localFrom + pos));
							_indexOfElidedBlock = blockIndex + (nextBlock ? 1 : 0);
							setElideBidi(_localFrom + pos, kQEllipsis.size());
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
						} else {
							lineText = lineText.mid(0, pos);
							lineLength = _localFrom + pos - _lineStart;
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
							prepareElidedLine(lineText, lineStart, lineLength, _endBlock, repeat + 1);
						}
						return;
					} else {
						_wLeft -= adv;
					}
				}
			}
		}

		int32 elideStart = _localFrom + lineText.size();
		_selection.to = qMin(_selection.to, uint16(elideStart));
		_indexOfElidedBlock = blockIndex + (nextBlock ? 1 : 0);
		setElideBidi(elideStart, kQEllipsis.size());

		lineText += kQEllipsis;
		lineLength += kQEllipsis.size();

		if (!repeat) {
			for (; blockIndex < _blocksSize && _t->_blocks[blockIndex].get() != _endBlock && _t->_blocks[blockIndex]->from() < elideStart; ++blockIndex) {
			}
			if (blockIndex < _blocksSize) {
				elideSaveBlock(blockIndex, _endBlock, elideStart, elideWidth);
			}
		}
	}

	void restoreAfterElided() {
		if (_elideSavedBlock) {
			const_cast<String*>(_t)->_blocks[_elideSavedIndex] = std::move(*_elideSavedBlock);
		}
	}

	// COPIED FROM qtextengine.cpp AND MODIFIED
	void eShapeLine(const QScriptLine &line) {
		int item = _e->findItem(line.from);
		if (item == -1)
			return;

		auto end = _e->findItem(line.from + line.length - 1, item);
		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
		eSetFont(currentBlock);
		for (; item <= end; ++item) {
			QScriptItem &si = _e->layoutData->items[item];
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
				eSetFont(currentBlock);
			}
			_e->shape(item);
		}
	}

	style::font applyFlags(int32 flags, const style::font &f) {
		if (!flags) {
			return f;
		}
		auto result = f;
		if (IsMono(flags)) {
			result = result->monospace();
		} else {
			if (flags & TextBlockFBold) {
				result = result->bold();
			} else if (flags & TextBlockFSemibold) {
				result = result->semibold();
			}
			if (flags & TextBlockFItalic) result = result->italic();
			if (flags & TextBlockFUnderline) result = result->underline();
			if (flags & TextBlockFStrikeOut) result = result->strikeout();
			if (flags & TextBlockFTilde) { // tilde fix in OpenSans
				result = result->semibold();
			}
		}
		return result;
	}

	void eSetFont(const AbstractBlock *block) {
		const auto flags = block->flags();
		const auto usedFont = [&] {
			if (const auto index = block->lnkIndex()) {
				const auto active = ClickHandler::showAsActive(
					_t->_links.at(index - 1)
				) || (_textPalette && _textPalette->linkAlwaysActive > 0);
				return active
					? _t->_st->linkFontOver
					: _t->_st->linkFont;
			}
			return _t->_st->font;
		}();
		const auto newFont = applyFlags(flags, usedFont);
		if (newFont != _f) {
			_f = (newFont->family() == _t->_st->font->family())
				? applyFlags(flags | newFont->flags(), _t->_st->font)
				: newFont;
			_e->fnt = _f->f;
			_e->resetFontEngineCache();
		}
	}

	void eItemize() {
		_e->validate();
		if (_e->layoutData->items.size())
			return;

		int length = _e->layoutData->string.length();
		if (!length)
			return;

		const ushort *string = reinterpret_cast<const ushort*>(_e->layoutData->string.unicode());

		auto blockIndex = _lineStartBlock;
		auto currentBlock = _t->_blocks[blockIndex].get();
		auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		_e->layoutData->hasBidi = _parHasBidi;
		auto analysis = _parAnalysis.data() + (_localFrom - _parStart);

		{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
			QUnicodeTools::ScriptItemArray scriptItems;
			QUnicodeTools::initScripts(_e->layoutData->string, &scriptItems);
			for (int i = 0; i < scriptItems.length(); ++i) {
				const auto &item = scriptItems.at(i);
				int end = i < scriptItems.length() - 1 ? scriptItems.at(i + 1).position : length;
				for (int j = item.position; j < end; ++j)
					analysis[j].script = item.script;
			}
#else // Qt >= 6.0.0
			QVarLengthArray<uchar> scripts(length);
			QUnicodeTools::initScripts(string, length, scripts.data());
			for (int i = 0; i < length; ++i)
				analysis[i].script = scripts.at(i);
#endif // Qt < 6.0.0
		}

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex].get();
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

		auto start = string;
		auto end = start + length;
		while (start < end) {
			while (nextBlock && nextBlock->from() <= _localFrom + (start - string)) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			}
			auto _type = currentBlock->type();
			if (_type == TextBlockTEmoji
				|| _type == TextBlockTCustomEmoji
				|| _type == TextBlockTSkip) {
				analysis->script = QChar::Script_Common;
				analysis->flags = QScriptAnalysis::Object;
			} else {
				analysis->flags = QScriptAnalysis::None;
			}
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
			analysis->script = hbscript_to_script(script_to_hbscript(analysis->script)); // retain the old behavior
#endif // Qt < 6.0.0
			++start;
			++analysis;
		}

		{
			auto i_string = &_e->layoutData->string;
			auto i_analysis = _parAnalysis.data() + (_localFrom - _parStart);
			auto i_items = &_e->layoutData->items;

			blockIndex = _lineStartBlock;
			currentBlock = _t->_blocks[blockIndex].get();
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			auto startBlock = currentBlock;

			if (!length) {
				return;
			}
			auto start = 0;
			auto end = start + length;
			for (int i = start + 1; i < end; ++i) {
				while (nextBlock && nextBlock->from() <= _localFrom + i) {
					currentBlock = nextBlock;
					nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
				}
				// According to the unicode spec we should be treating characters in the Common script
				// (punctuation, spaces, etc) as being the same script as the surrounding text for the
				// purpose of splitting up text. This is important because, for example, a fullstop
				// (0x2E) can be used to indicate an abbreviation and so must be treated as part of a
				// word.  Thus it must be passed along with the word in languages that have to calculate
				// word breaks.  For example the thai word "ครม." has no word breaks but the word "ครม"
				// does.
				// Unfortuntely because we split up the strings for both wordwrapping and for setting
				// the font and because Japanese and Chinese are also aliases of the script "Common",
				// doing this would break too many things.  So instead we only pass the full stop
				// along, and nothing else.
				if (currentBlock == startBlock
					&& i_analysis[i].bidiLevel == i_analysis[start].bidiLevel
					&& i_analysis[i].flags == i_analysis[start].flags
					&& (i_analysis[i].script == i_analysis[start].script || i_string->at(i) == QLatin1Char('.'))
//					&& i_analysis[i].flags < QScriptAnalysis::SpaceTabOrObject // only emojis are objects here, no tabs
					&& i - start < _MaxItemLength)
					continue;
				i_items->append(QScriptItem(start, i_analysis[start]));
				start = i;
				startBlock = currentBlock;
			}
			i_items->append(QScriptItem(start, i_analysis[start]));
		}
	}

	QChar::Direction eSkipBoundryNeutrals(QScriptAnalysis *analysis,
											const ushort *unicode,
											int &sor, int &eor, BidiControl &control,
											String::TextBlocks::const_iterator i) {
		String::TextBlocks::const_iterator e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction dir = control.basicDirection();
		int level = sor > 0 ? analysis[sor - 1].bidiLevel : control.level;
		while (sor <= _parLength) {
			while (i != _parStartBlock && (*i)->from() > _parStart + sor) {
				n = i;
				--i;
			}
			while (n != e && (*n)->from() <= _parStart + sor) {
				i = n;
				++n;
			}

			TextBlockType _itype = (*i)->type();
			if (eor == _parLength)
				dir = control.basicDirection();
			else if (_itype == TextBlockTEmoji
				|| _itype == TextBlockTCustomEmoji)
				dir = QChar::DirCS;
			else if (_itype == TextBlockTSkip)
				dir = QChar::DirCS;
			else
				dir = QChar::direction(unicode[sor]);
			// Keep skipping DirBN as if it doesn't exist
			if (dir != QChar::DirBN)
				break;
			analysis[sor++].bidiLevel = level;
		}

		eor = sor;

		return dir;
	}

	// creates the next QScript items.
	bool eBidiItemize(QScriptAnalysis *analysis, BidiControl &control) {
		bool rightToLeft = (control.basicDirection() == 1);
		bool hasBidi = rightToLeft;

		int sor = 0;
		int eor = -1;

		const ushort *unicode = reinterpret_cast<const ushort*>(_t->_text.unicode()) + _parStart;
		int current = 0;

		QChar::Direction dir = rightToLeft ? QChar::DirR : QChar::DirL;
		BidiStatus status;

		String::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction sdir;
		TextBlockType _stype = (*_parStartBlock)->type();
		if (_stype == TextBlockTEmoji || _stype == TextBlockTCustomEmoji)
			sdir = QChar::DirCS;
		else if (_stype == TextBlockTSkip)
			sdir = QChar::DirCS;
		else
			sdir = QChar::direction(*unicode);
		if (sdir != QChar::DirL && sdir != QChar::DirR && sdir != QChar::DirEN && sdir != QChar::DirAN)
			sdir = QChar::DirON;
		else
			dir = QChar::DirON;

		status.eor = sdir;
		status.lastStrong = rightToLeft ? QChar::DirR : QChar::DirL;
		status.last = status.lastStrong;
		status.dir = sdir;

		while (current <= _parLength) {
			while (n != e && (*n)->from() <= _parStart + current) {
				i = n;
				++n;
			}

			QChar::Direction dirCurrent;
			TextBlockType _itype = (*i)->type();
			if (current == (int)_parLength)
				dirCurrent = control.basicDirection();
			else if (_itype == TextBlockTEmoji
				|| _itype == TextBlockTCustomEmoji)
				dirCurrent = QChar::DirCS;
			else if (_itype == TextBlockTSkip)
				dirCurrent = QChar::DirCS;
			else
				dirCurrent = QChar::direction(unicode[current]);

			switch (dirCurrent) {

				// embedding and overrides (X1-X9 in the BiDi specs)
			case QChar::DirRLE:
			case QChar::DirRLO:
			case QChar::DirLRE:
			case QChar::DirLRO:
				{
					bool rtl = (dirCurrent == QChar::DirRLE || dirCurrent == QChar::DirRLO);
					hasBidi |= rtl;
					bool override = (dirCurrent == QChar::DirLRO || dirCurrent == QChar::DirRLO);

					unsigned int level = control.level+1;
					if ((level%2 != 0) == rtl) ++level;
					if (level < _MaxBidiLevel) {
						eor = current-1;
						eAppendItems(analysis, sor, eor, control, dir);
						eor = current;
						control.embed(rtl, override);
						QChar::Direction edir = (rtl ? QChar::DirR : QChar::DirL);
						dir = status.eor = edir;
						status.lastStrong = edir;
					}
					break;
				}
			case QChar::DirPDF:
				{
					if (control.canPop()) {
						if (dir != control.direction()) {
							eor = current-1;
							eAppendItems(analysis, sor, eor, control, dir);
							dir = control.direction();
						}
						eor = current;
						eAppendItems(analysis, sor, eor, control, dir);
						control.pdf();
						dir = QChar::DirON; status.eor = QChar::DirON;
						status.last = control.direction();
						if (control.override)
							dir = control.direction();
						else
							dir = QChar::DirON;
						status.lastStrong = control.direction();
					}
					break;
				}

				// strong types
			case QChar::DirL:
				if(dir == QChar::DirON)
					dir = QChar::DirL;
				switch(status.last)
					{
					case QChar::DirL:
						eor = current; status.eor = QChar::DirL; break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0) {
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
						} else {
							eor = current; status.eor = dir;
						}
						break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(dir != QChar::DirL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR) {
								if(status.eor != QChar::DirR) {
									// AN or EN
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirR;
								}
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
							} else {
								if(status.eor != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirL;
								} else {
									eor = current; status.eor = QChar::DirL; break;
								}
							}
						} else {
							eor = current; status.eor = QChar::DirL;
						}
					default:
						break;
					}
				status.lastStrong = QChar::DirL;
				break;
			case QChar::DirAL:
			case QChar::DirR:
				hasBidi = true;
				if(dir == QChar::DirON) dir = QChar::DirR;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0)
							eAppendItems(analysis, sor, eor, control, dir);
						// fall through
					case QChar::DirR:
					case QChar::DirAL:
						dir = QChar::DirR; eor = current; status.eor = QChar::DirR; break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor != QChar::DirR && status.eor != QChar::DirAL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR
							   || status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
								eor = current;
							} else {
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
							}
						} else {
							eor = current; status.eor = QChar::DirR;
						}
					default:
						break;
					}
				status.lastStrong = dirCurrent;
				break;

				// weak types:

			case QChar::DirNSM:
				if (eor == current-1)
					eor = current;
				break;
			case QChar::DirEN:
				// if last strong was AL change EN to AN
				if(status.lastStrong != QChar::DirAL) {
					if(dir == QChar::DirON) {
						if(status.lastStrong == QChar::DirL)
							dir = QChar::DirL;
						else
							dir = QChar::DirEN;
					}
					switch(status.last)
						{
						case QChar::DirET:
							if (status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								dir = QChar::DirAN;
							}
							[[fallthrough]];
						case QChar::DirEN:
						case QChar::DirL:
							eor = current;
							status.eor = dirCurrent;
							break;
						case QChar::DirR:
						case QChar::DirAL:
						case QChar::DirAN:
							if (eor >= 0)
								eAppendItems(analysis, sor, eor, control, dir);
							else
								eor = current;
							status.eor = QChar::DirEN;
							dir = QChar::DirAN;
							break;
						case QChar::DirES:
						case QChar::DirCS:
							if(status.eor == QChar::DirEN || dir == QChar::DirAN) {
								eor = current; break;
							}
							[[fallthrough]];
						case QChar::DirBN:
						case QChar::DirB:
						case QChar::DirS:
						case QChar::DirWS:
						case QChar::DirON:
							if(status.eor == QChar::DirR) {
								// neutrals go to R
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirEN;
								dir = QChar::DirAN;
							}
							else if(status.eor == QChar::DirL ||
									 (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
								eor = current; status.eor = dirCurrent;
							} else {
								// numbers on both sides, neutrals get right to left direction
								if(dir != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									eor = current - 1;
									dir = QChar::DirR;
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirAN;
								} else {
									eor = current; status.eor = dirCurrent;
								}
							}
							[[fallthrough]];
						default:
							break;
						}
					break;
				}
				[[fallthrough]];
			case QChar::DirAN:
				hasBidi = true;
				dirCurrent = QChar::DirAN;
				if(dir == QChar::DirON) dir = QChar::DirAN;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirAN:
						eor = current; status.eor = QChar::DirAN;
						break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
						if (eor >= 0){
							eAppendItems(analysis, sor, eor, control, dir);
						} else {
							eor = current;
						}
						dir = QChar::DirAN; status.eor = QChar::DirAN;
						break;
					case QChar::DirCS:
						if(status.eor == QChar::DirAN) {
							eor = current; break;
						}
						[[fallthrough]];
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor == QChar::DirR) {
							// neutrals go to R
							eor = current - 1;
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirAN;
							dir = QChar::DirAN;
						} else if(status.eor == QChar::DirL ||
								   (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
							eor = current; status.eor = dirCurrent;
						} else {
							// numbers on both sides, neutrals get right to left direction
							if(dir != QChar::DirL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								eor = current - 1;
								dir = QChar::DirR;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirAN;
								dir = QChar::DirAN;
							} else {
								eor = current; status.eor = dirCurrent;
							}
						}
						[[fallthrough]];
					default:
						break;
					}
				break;
			case QChar::DirES:
			case QChar::DirCS:
				break;
			case QChar::DirET:
				if(status.last == QChar::DirEN) {
					dirCurrent = QChar::DirEN;
					eor = current; status.eor = dirCurrent;
				}
				break;

				// boundary neutrals should be ignored
			case QChar::DirBN:
				break;
				// neutrals
			case QChar::DirB:
				// ### what do we do with newline and paragraph separators that come to here?
				break;
			case QChar::DirS:
				// ### implement rule L1
				break;
			case QChar::DirWS:
			case QChar::DirON:
				break;
			default:
				break;
			}

			if(current >= (int)_parLength) break;

			// set status.last as needed.
			switch(dirCurrent) {
			case QChar::DirET:
			case QChar::DirES:
			case QChar::DirCS:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
				switch(status.last)
				{
				case QChar::DirL:
				case QChar::DirR:
				case QChar::DirAL:
				case QChar::DirEN:
				case QChar::DirAN:
					status.last = dirCurrent;
					break;
				default:
					status.last = QChar::DirON;
				}
				break;
			case QChar::DirNSM:
			case QChar::DirBN:
				// ignore these
				break;
			case QChar::DirLRO:
			case QChar::DirLRE:
				status.last = QChar::DirL;
				break;
			case QChar::DirRLO:
			case QChar::DirRLE:
				status.last = QChar::DirR;
				break;
			case QChar::DirEN:
				if (status.last == QChar::DirL) {
					status.last = QChar::DirL;
					break;
				}
				[[fallthrough]];
			default:
				status.last = dirCurrent;
			}

			++current;
		}

		eor = current - 1; // remove dummy char

		if (sor <= eor)
			eAppendItems(analysis, sor, eor, control, dir);

		return hasBidi;
	}

private:
	void applyBlockProperties(const AbstractBlock *block) {
		eSetFont(block);
		if (_p) {
			const auto isMono = IsMono(block->flags());
			_background = {};
			if (block->spoilerIndex()) {
				const auto handler
					= _t->_spoilers.at(block->spoilerIndex() - 1);
				const auto inBack = (handler && handler->shown());
				_background.inFront = !inBack;
				_background.color = inBack
					? &_textPalette->spoilerActiveBg
					: &_textPalette->spoilerBg;
				_background.startMs = handler ? handler->startMs() : 0;
				_background.spoilerIndex = block->spoilerIndex();

				const auto &cache = _background.inFront
					? _t->_spoilerCache
					: _t->_spoilerShownCache;
				if (cache.color != (*_background.color)->c) {
					auto mutableText = const_cast<String*>(_t);
					auto &mutableCache = _background.inFront
						? mutableText->_spoilerCache
						: mutableText->_spoilerShownCache;
					mutableCache.corners = Images::PrepareCorners(
						ImageRoundRadius::Small,
						*_background.color);
					mutableCache.color = (*_background.color)->c;
				}
			}
			if (isMono && block->lnkIndex() && !_background.inFront) {
				_background.selectActiveBlock = ClickHandler::showAsPressed(
					_t->_links.at(block->lnkIndex() - 1));
			}

			if (isMono) {
				_currentPen = &_textPalette->monoFg->p;
				_currentPenSelected = &_textPalette->selectMonoFg->p;
			} else if (block->lnkIndex()
				|| (block->flags() & TextBlockFPlainLink)) {
				_currentPen = &_textPalette->linkFg->p;
				_currentPenSelected = &_textPalette->selectLinkFg->p;
			} else {
				_currentPen = &_originalPen;
				_currentPenSelected = &_originalPenSelected;
			}
		}
	}

	Painter *_p = nullptr;
	const style::TextPalette *_textPalette = nullptr;
	const String *_t = nullptr;
	bool _elideLast = false;
	bool _breakEverywhere = false;
	int _elideRemoveFromEnd = 0;
	style::align _align = style::al_topleft;
	const QPen _originalPen;
	QPen _originalPenSelected;
	const QPen *_currentPen = nullptr;
	const QPen *_currentPenSelected = nullptr;
	struct {
		const style::color *color = nullptr;
		bool inFront = false;
		crl::time startMs = 0;
		uint16 spoilerIndex = 0;

		bool selectActiveBlock = false; // For monospace.
	} _background;
	int _yFrom = 0;
	int _yTo = 0;
	int _yToElide = 0;
	TextSelection _selection = { 0, 0 };
	bool _fullWidthSelection = true;
	const QChar *_str = nullptr;
	crl::time _now = 0;

	int _customEmojiSize = 0;
	int _customEmojiSkip = 0;
	int _indexOfElidedBlock = -1; // For spoilers.

	// current paragraph data
	String::TextBlocks::const_iterator _parStartBlock;
	Qt::LayoutDirection _parDirection;
	int _parStart = 0;
	int _parLength = 0;
	bool _parHasBidi = false;
	QVarLengthArray<QScriptAnalysis, 4096> _parAnalysis;

	// current line data
	QTextEngine *_e = nullptr;
	style::font _f;
	QFixed _x, _w, _wLeft, _last_rPadding;
	int32 _y, _yDelta, _lineHeight, _fontHeight;

	// elided hack support
	int _blocksSize = 0;
	int _elideSavedIndex = 0;
	std::optional<Block> _elideSavedBlock;

	int _lineStart = 0;
	int _localFrom = 0;
	int _lineStartBlock = 0;

	// link and symbol resolve
	QFixed _lookupX = 0;
	int _lookupY = 0;
	bool _lookupSymbol = false;
	bool _lookupLink = false;
	StateRequest _lookupRequest;
	StateResult _lookupResult;

};

String::String(int32 minResizeWidth) : _minResizeWidth(minResizeWidth) {
}

String::String(const style::TextStyle &st, const QString &text, const TextParseOptions &options, int32 minResizeWidth)
: _minResizeWidth(minResizeWidth) {
	setText(st, text, options);
}

void String::setText(const style::TextStyle &st, const QString &text, const TextParseOptions &options) {
	_st = &st;
	clear();
	{
		Parser parser(this, { text }, options, {});
	}
	recountNaturalSize(true, options.dir);
}

void String::recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir) {
	NewlineBlock *lastNewline = 0;

	_maxWidth = _minHeight = 0;
	int32 lineHeight = 0;
	int32 lastNewlineStart = 0;
	QFixed _width = 0, last_rBearing = 0, last_rPadding = 0;
	for (auto &block : _blocks) {
		auto b = block.get();
		auto _btype = b->type();
		auto blockHeight = countBlockHeight(b, _st);
		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			if (initial) {
				Qt::LayoutDirection dir = optionsDir;
				if (dir == Qt::LayoutDirectionAuto) {
					dir = StringDirection(_text, lastNewlineStart, b->from());
				}
				if (lastNewline) {
					lastNewline->_nextDir = dir;
				} else {
					_startDir = dir;
				}
			}
			lastNewlineStart = b->from();
			lastNewline = &block.unsafe<NewlineBlock>();

			_minHeight += lineHeight;
			lineHeight = 0;
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();

			accumulate_max(_maxWidth, _width);
			_width = (b->f_width() - last_rBearing);
			continue;
		}

		auto b__f_rbearing = b->f_rbearing(); // cache

		// We need to accumulate max width after each block, because
		// some blocks have width less than -1 * previous right bearing.
		// In that cases the _width gets _smaller_ after moving to the next block.
		//
		// But when we layout block and we're sure that _maxWidth is enough
		// for all the blocks to fit on their line we check each block, even the
		// intermediate one with a large negative right bearing.
		accumulate_max(_maxWidth, _width);

		_width += last_rBearing + (last_rPadding + b->f_width() - b__f_rbearing);
		lineHeight = qMax(lineHeight, blockHeight);

		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		continue;
	}
	if (initial) {
		Qt::LayoutDirection dir = optionsDir;
		if (dir == Qt::LayoutDirectionAuto) {
			dir = StringDirection(_text, lastNewlineStart, _text.size());
		}
		if (lastNewline) {
			lastNewline->_nextDir = dir;
		} else {
			_startDir = dir;
		}
	}
	if (_width > 0) {
		if (!lineHeight) lineHeight = countBlockHeight(_blocks.back().get(), _st);
		_minHeight += lineHeight;
		accumulate_max(_maxWidth, _width);
	}
}

int String::countMaxMonospaceWidth() const {
	auto result = QFixed();
	auto paragraphWidth = QFixed();
	auto fullMonospace = true;
	QFixed _width = 0, last_rBearing = 0, last_rPadding = 0;
	for (auto &block : _blocks) {
		auto b = block.get();
		auto _btype = b->type();
		if (_btype == TextBlockTNewline) {
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();

			if (fullMonospace) {
				accumulate_max(paragraphWidth, _width);
				accumulate_max(result, paragraphWidth);
				paragraphWidth = 0;
			} else {
				fullMonospace = true;
			}
			_width = (b->f_width() - last_rBearing);
			continue;
		}
		if (!(b->flags() & (TextBlockFPre | TextBlockFCode))
			&& (b->type() != TextBlockTSkip)) {
			fullMonospace = false;
		}
		auto b__f_rbearing = b->f_rbearing(); // cache

		// We need to accumulate max width after each block, because
		// some blocks have width less than -1 * previous right bearing.
		// In that cases the _width gets _smaller_ after moving to the next block.
		//
		// But when we layout block and we're sure that _maxWidth is enough
		// for all the blocks to fit on their line we check each block, even the
		// intermediate one with a large negative right bearing.
		if (fullMonospace) {
			accumulate_max(paragraphWidth, _width);
		}
		_width += last_rBearing + (last_rPadding + b->f_width() - b__f_rbearing);

		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		continue;
	}
	if (_width > 0 && fullMonospace) {
		accumulate_max(paragraphWidth, _width);
		accumulate_max(result, paragraphWidth);
	}
	return result.ceil().toInt();
}

void String::setMarkedText(const style::TextStyle &st, const TextWithEntities &textWithEntities, const TextParseOptions &options, const std::any &context) {
	_st = &st;
	clear();
	{
		// utf codes of the text display for emoji extraction
//		auto text = textWithEntities.text;
//		auto newText = QString();
//		newText.reserve(8 * text.size());
//		newText.append("\t{ ");
//		for (const QChar *ch = text.constData(), *e = ch + text.size(); ch != e; ++ch) {
//			if (*ch == TextCommand) {
//				break;
//			} else if (IsNewline(*ch)) {
//				newText.append("},").append(*ch).append("\t{ ");
//			} else {
//				if (ch->isHighSurrogate() || ch->isLowSurrogate()) {
//					if (ch->isHighSurrogate() && (ch + 1 != e) && ((ch + 1)->isLowSurrogate())) {
//						newText.append("0x").append(QString::number((uint32(ch->unicode()) << 16) | uint32((ch + 1)->unicode()), 16).toUpper()).append("U, ");
//						++ch;
//					} else {
//						newText.append("BADx").append(QString::number(ch->unicode(), 16).toUpper()).append("U, ");
//					}
//				} else {
//					newText.append("0x").append(QString::number(ch->unicode(), 16).toUpper()).append("U, ");
//				}
//			}
//		}
//		newText.append("},\n\n").append(text);
//		Parser parser(this, { newText, EntitiesInText() }, options, context);

		Parser parser(this, textWithEntities, options, context);
	}
	recountNaturalSize(true, options.dir);
}

void String::setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk) {
	if (!lnkIndex || lnkIndex > _links.size()) return;
	_links[lnkIndex - 1] = lnk;
}

void String::setSpoiler(
		uint16 lnkIndex,
		const std::shared_ptr<SpoilerClickHandler> &lnk) {
	if (!lnkIndex || lnkIndex > _spoilers.size()) {
		return;
	}
	_spoilers[lnkIndex - 1] = lnk;
}

void String::setSpoilerShown(uint16 lnkIndex, bool shown) {
	if (!lnkIndex
		|| (lnkIndex > _spoilers.size())
		|| !_spoilers[lnkIndex - 1]) {
		return;
	}
	_spoilers[lnkIndex - 1]->setShown(shown);
}

bool String::hasLinks() const {
	return !_links.isEmpty();
}

int String::spoilersCount() const {
	return _spoilers.size();
}

bool String::hasSkipBlock() const {
	return _blocks.empty() ? false : _blocks.back()->type() == TextBlockTSkip;
}

bool String::updateSkipBlock(int width, int height) {
	if (!_blocks.empty() && _blocks.back()->type() == TextBlockTSkip) {
		const auto block = static_cast<SkipBlock*>(_blocks.back().get());
		if (block->width() == width && block->height() == height) {
			return false;
		}
		_text.resize(block->from());
		_blocks.pop_back();
	}
	_text.push_back('_');
	_blocks.push_back(Block::Skip(
		_st->font,
		_text,
		_text.size() - 1,
		width,
		height,
		0,
		0));
	recountNaturalSize(false);
	return true;
}

bool String::removeSkipBlock() {
	if (_blocks.empty() || _blocks.back()->type() != TextBlockTSkip) {
		return false;
	}
	_text.resize(_blocks.back()->from());
	_blocks.pop_back();
	recountNaturalSize(false);
	return true;
}

int String::countWidth(int width, bool breakEverywhere) const {
	if (QFixed(width) >= _maxWidth) {
		return _maxWidth.ceil().toInt();
	}

	QFixed maxLineWidth = 0;
	enumerateLines(width, breakEverywhere, [&](QFixed lineWidth, int lineHeight) {
		if (lineWidth > maxLineWidth) {
			maxLineWidth = lineWidth;
		}
	});
	return maxLineWidth.ceil().toInt();
}

int String::countHeight(int width, bool breakEverywhere) const {
	if (QFixed(width) >= _maxWidth) {
		return _minHeight;
	}
	int result = 0;
	enumerateLines(width, breakEverywhere, [&](QFixed lineWidth, int lineHeight) {
		result += lineHeight;
	});
	return result;
}

void String::countLineWidths(int width, QVector<int> *lineWidths, bool breakEverywhere) const {
	enumerateLines(width, breakEverywhere, [&](QFixed lineWidth, int lineHeight) {
		lineWidths->push_back(lineWidth.ceil().toInt());
	});
}

template <typename Callback>
void String::enumerateLines(
		int w,
		bool breakEverywhere,
		Callback callback) const {
	QFixed width = w;
	if (width < _minResizeWidth) width = _minResizeWidth;

	int lineHeight = 0;
	QFixed widthLeft = width, last_rBearing = 0, last_rPadding = 0;
	bool longWordLine = true;
	for (auto &b : _blocks) {
		auto _btype = b->type();
		int blockHeight = countBlockHeight(b.get(), _st);

		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			callback(width - widthLeft, lineHeight);

			lineHeight = 0;
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();
			widthLeft = width - (b->f_width() - last_rBearing);

			longWordLine = true;
			continue;
		}
		auto b__f_rbearing = b->f_rbearing();
		auto newWidthLeft = widthLeft - last_rBearing - (last_rPadding + b->f_width() - b__f_rbearing);
		if (newWidthLeft >= 0) {
			last_rBearing = b__f_rbearing;
			last_rPadding = b->f_rpadding();
			widthLeft = newWidthLeft;

			lineHeight = qMax(lineHeight, blockHeight);

			longWordLine = false;
			continue;
		}

		if (_btype == TextBlockTText) {
			const auto t = &b.unsafe<TextBlock>();
			if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
				last_rPadding += b->f_rpadding();

				lineHeight = qMax(lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			auto f_wLeft = widthLeft;
			int f_lineHeight = lineHeight;
			for (auto j = t->_words.cbegin(), e = t->_words.cend(), f = j; j != e; ++j) {
				bool wordEndsHere = (j->f_width() >= 0);
				auto j_width = wordEndsHere ? j->f_width() : -j->f_width();

				auto newWidthLeft = widthLeft - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
				if (newWidthLeft >= 0) {
					last_rBearing = j->f_rbearing();
					last_rPadding = j->f_rpadding();
					widthLeft = newWidthLeft;

					lineHeight = qMax(lineHeight, blockHeight);

					if (wordEndsHere) {
						longWordLine = false;
					}
					if (wordEndsHere || longWordLine) {
						f_wLeft = widthLeft;
						f_lineHeight = lineHeight;
						f = j + 1;
					}
					continue;
				}

				if (f != j && !breakEverywhere) {
					j = f;
					widthLeft = f_wLeft;
					lineHeight = f_lineHeight;
					j_width = (j->f_width() >= 0) ? j->f_width() : -j->f_width();
				}

				callback(width - widthLeft, lineHeight);

				lineHeight = qMax(0, blockHeight);
				last_rBearing = j->f_rbearing();
				last_rPadding = j->f_rpadding();
				widthLeft = width - (j_width - last_rBearing);

				longWordLine = !wordEndsHere;
				f = j + 1;
				f_wLeft = widthLeft;
				f_lineHeight = lineHeight;
			}
			continue;
		}

		callback(width - widthLeft, lineHeight);

		lineHeight = qMax(0, blockHeight);
		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		widthLeft = width - (b->f_width() - last_rBearing);

		longWordLine = true;
		continue;
	}
	if (widthLeft < width) {
		callback(width - widthLeft, lineHeight);
	}
}

void String::draw(Painter &painter, int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, TextSelection selection, bool fullWidthSelection) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	Renderer p(&painter, this);
	p.draw(left, top, w, align, yFrom, yTo, selection, fullWidthSelection);
}

void String::drawElided(Painter &painter, int32 left, int32 top, int32 w, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	Renderer p(&painter, this);
	p.drawElided(left, top, w, align, lines, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
}

void String::drawLeft(Painter &p, int32 left, int32 top, int32 width, int32 outerw, style::align align, int32 yFrom, int32 yTo, TextSelection selection) const {
	draw(p, style::RightToLeft() ? (outerw - left - width) : left, top, width, align, yFrom, yTo, selection);
}

void String::drawLeftElided(Painter &p, int32 left, int32 top, int32 width, int32 outerw, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
	drawElided(p, style::RightToLeft() ? (outerw - left - width) : left, top, width, lines, align, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
}

void String::drawRight(Painter &p, int32 right, int32 top, int32 width, int32 outerw, style::align align, int32 yFrom, int32 yTo, TextSelection selection) const {
	draw(p, style::RightToLeft() ? right : (outerw - right - width), top, width, align, yFrom, yTo, selection);
}

void String::drawRightElided(Painter &p, int32 right, int32 top, int32 width, int32 outerw, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
	drawElided(p, style::RightToLeft() ? right : (outerw - right - width), top, width, lines, align, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
}

StateResult String::getState(QPoint point, int width, StateRequest request) const {
	return Renderer(nullptr, this).getState(point, width, request);
}

StateResult String::getStateLeft(QPoint point, int width, int outerw, StateRequest request) const {
	return getState(style::rtlpoint(point, outerw), width, request);
}

StateResult String::getStateElided(QPoint point, int width, StateRequestElided request) const {
	return Renderer(nullptr, this).getStateElided(point, width, request);
}

StateResult String::getStateElidedLeft(QPoint point, int width, int outerw, StateRequestElided request) const {
	return getStateElided(style::rtlpoint(point, outerw), width, request);
}

TextSelection String::adjustSelection(TextSelection selection, TextSelectType selectType) const {
	uint16 from = selection.from, to = selection.to;
	if (from < _text.size() && from <= to) {
		if (to > _text.size()) to = _text.size();
		if (selectType == TextSelectType::Paragraphs) {

			// Full selection of monospace entity.
			for (const auto &b : _blocks) {
				if (b->from() < from) {
					continue;
				}
				if (!IsMono(b->flags())) {
					break;
				}
				const auto &entities = toTextWithEntities().entities;
				const auto eIt = ranges::find_if(entities, [&](
						const EntityInText &e) {
					return (e.type() == EntityType::Pre
							|| e.type() == EntityType::Code)
						&& (from >= e.offset())
						&& ((e.offset() + e.length()) >= to);
				});
				if (eIt != entities.end()) {
					from = eIt->offset();
					to = eIt->offset() + eIt->length();
					while (to > 0 && IsSpace(_text.at(to - 1))) {
						--to;
					}
					if (to >= from) {
						return { from, to };
					}
				}
				break;
			}

			if (!IsParagraphSeparator(_text.at(from))) {
				while (from > 0 && !IsParagraphSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (IsParagraphSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !IsParagraphSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		} else if (selectType == TextSelectType::Words) {
			if (!IsWordSeparator(_text.at(from))) {
				while (from > 0 && !IsWordSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (IsWordSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !IsWordSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		}
	}
	return { from, to };
}

bool String::isEmpty() const {
	return _blocks.empty() || _blocks[0]->type() == TextBlockTSkip;
}

uint16 String::countBlockEnd(const TextBlocks::const_iterator &i, const TextBlocks::const_iterator &e) const {
	return (i + 1 == e) ? _text.size() : (*(i + 1))->from();
}

uint16 String::countBlockLength(const String::TextBlocks::const_iterator &i, const String::TextBlocks::const_iterator &e) const {
	return countBlockEnd(i, e) - (*i)->from();
}

template <
	typename AppendPartCallback,
	typename ClickHandlerStartCallback,
	typename ClickHandlerFinishCallback,
	typename FlagsChangeCallback>
void String::enumerateText(
		TextSelection selection,
		AppendPartCallback appendPartCallback,
		ClickHandlerStartCallback clickHandlerStartCallback,
		ClickHandlerFinishCallback clickHandlerFinishCallback,
		FlagsChangeCallback flagsChangeCallback) const {
	if (isEmpty() || selection.empty()) {
		return;
	}

	int lnkIndex = 0;
	uint16 lnkFrom = 0;

	int spoilerIndex = 0;
	uint16 spoilerFrom = 0;

	int32 flags = 0;
	for (auto i = _blocks.cbegin(), e = _blocks.cend(); true; ++i) {
		int blockLnkIndex = (i == e) ? 0 : (*i)->lnkIndex();
		int blockSpoilerIndex = (i == e) ? 0 : (*i)->spoilerIndex();
		uint16 blockFrom = (i == e) ? _text.size() : (*i)->from();
		int32 blockFlags = (i == e) ? 0 : (*i)->flags();

		if (IsMono(blockFlags)) {
			blockLnkIndex = 0;
		}
		if (blockLnkIndex && !_links.at(blockLnkIndex - 1)) { // ignore empty links
			blockLnkIndex = 0;
		}
		if (blockLnkIndex != lnkIndex) {
			if (lnkIndex) {
				auto rangeFrom = qMax(selection.from, lnkFrom);
				auto rangeTo = qMin(selection.to, blockFrom);
				if (rangeTo > rangeFrom) { // handle click handler
					const auto r = base::StringViewMid(_text, rangeFrom, rangeTo - rangeFrom);
					// Ignore links that are partially copied.
					const auto handler = (lnkFrom != rangeFrom || blockFrom != rangeTo)
						? nullptr
						: _links.at(lnkIndex - 1);
					const auto type = handler
						? handler->getTextEntity().type
						: EntityType::Invalid;
					clickHandlerFinishCallback(r, handler, type);
				}
			}
			lnkIndex = blockLnkIndex;
			if (lnkIndex) {
				lnkFrom = blockFrom;
				const auto handler = _links.at(lnkIndex - 1);
				clickHandlerStartCallback(handler
					? handler->getTextEntity().type
					: EntityType::Invalid);
			}
		}
		if (blockSpoilerIndex != spoilerIndex) {
			if (spoilerIndex) {
				auto rangeFrom = qMax(selection.from, spoilerFrom);
				auto rangeTo = qMin(selection.to, blockFrom);
				if (rangeTo > rangeFrom) { // handle click handler
					const auto r = base::StringViewMid(_text, rangeFrom, rangeTo - rangeFrom);
					// Ignore links that are partially copied.
					const auto handler = (spoilerFrom != rangeFrom || blockFrom != rangeTo)
						? nullptr
						: _spoilers.at(spoilerIndex - 1);
					const auto type = EntityType::Spoiler;
					clickHandlerFinishCallback(r, handler, type);
				}
			}
			spoilerIndex = blockSpoilerIndex;
			if (spoilerIndex) {
				spoilerFrom = blockFrom;
				clickHandlerStartCallback(EntityType::Spoiler);
			}
		}

		const auto checkBlockFlags = (blockFrom >= selection.from)
			&& (blockFrom <= selection.to);
		if (checkBlockFlags && blockFlags != flags) {
			flagsChangeCallback(flags, blockFlags);
			flags = blockFlags;
		}
		if (i == e || (lnkIndex ? lnkFrom : blockFrom) >= selection.to) {
			break;
		}

		const auto blockType = (*i)->type();
		if (blockType == TextBlockTSkip) continue;

		auto rangeFrom = qMax(selection.from, blockFrom);
		auto rangeTo = qMin(
			selection.to,
			uint16(blockFrom + countBlockLength(i, e)));
		if (rangeTo > rangeFrom) {
			const auto customEmojiData = (blockType == TextBlockTCustomEmoji)
				? static_cast<const CustomEmojiBlock*>(i->get())->_custom->entityData()
				: QString();
			appendPartCallback(
				base::StringViewMid(_text, rangeFrom, rangeTo - rangeFrom),
				customEmojiData);
		}
	}
}

bool String::hasCustomEmoji() const {
	return _hasCustomEmoji;
}

void String::unloadCustomEmoji() {
	if (!_hasCustomEmoji) {
		return;
	}
	for (const auto &block : _blocks) {
		const auto raw = block.get();
		if (raw->type() == TextBlockTCustomEmoji) {
			static_cast<const CustomEmojiBlock*>(raw)->_custom->unload();
		}
	}
}

QString String::toString(TextSelection selection) const {
	return toText(selection, false, false).rich.text;
}

TextWithEntities String::toTextWithEntities(TextSelection selection) const {
	return toText(selection, false, true).rich;
}

TextForMimeData String::toTextForMimeData(TextSelection selection) const {
	return toText(selection, true, true);
}

TextForMimeData String::toText(
		TextSelection selection,
		bool composeExpanded,
		bool composeEntities) const {
	struct MarkdownTagTracker {
		TextBlockFlags flag = TextBlockFlags();
		EntityType type = EntityType();
		int start = 0;
	};
	auto result = TextForMimeData();
	result.rich.text.reserve(_text.size());
	if (composeExpanded) {
		result.expanded.reserve(_text.size());
	}
	const auto insertEntity = [&](EntityInText &&entity) {
		auto i = result.rich.entities.end();
		while (i != result.rich.entities.begin()) {
			auto j = i;
			if ((--j)->offset() <= entity.offset()) {
				break;
			}
			i = j;
		}
		result.rich.entities.insert(i, std::move(entity));
	};
	auto linkStart = 0;
	auto spoilerStart = 0;
	auto markdownTrackers = composeEntities
		? std::vector<MarkdownTagTracker>{
			{ TextBlockFItalic, EntityType::Italic },
			{ TextBlockFBold, EntityType::Bold },
			{ TextBlockFSemibold, EntityType::Semibold },
			{ TextBlockFUnderline, EntityType::Underline },
			{ TextBlockFPlainLink, EntityType::PlainLink },
			{ TextBlockFStrikeOut, EntityType::StrikeOut },
			{ TextBlockFCode, EntityType::Code }, // #TODO entities
			{ TextBlockFPre, EntityType::Pre },
		} : std::vector<MarkdownTagTracker>();
	const auto flagsChangeCallback = [&](int32 oldFlags, int32 newFlags) {
		if (!composeEntities) {
			return;
		}
		for (auto &tracker : markdownTrackers) {
			const auto flag = tracker.flag;
			if ((oldFlags & flag) && !(newFlags & flag)) {
				insertEntity({
					tracker.type,
					tracker.start,
					int(result.rich.text.size()) - tracker.start });
			} else if ((newFlags & flag) && !(oldFlags & flag)) {
				tracker.start = result.rich.text.size();
			}
		}
	};
	const auto clickHandlerStartCallback = [&](EntityType type) {
		if (type == EntityType::Spoiler) {
			spoilerStart = result.rich.text.size();
		} else {
			linkStart = result.rich.text.size();
		}
	};
	const auto clickHandlerFinishCallback = [&](
			QStringView inText,
			const ClickHandlerPtr &handler,
			EntityType type) {
		if (type == EntityType::Spoiler) {
			insertEntity({
				type,
				spoilerStart,
				int(result.rich.text.size() - spoilerStart),
				QString(),
			});
			return;
		}
		if (!handler || (!composeExpanded && !composeEntities)) {
			return;
		}
		const auto entity = handler->getTextEntity();
		const auto plainUrl = (entity.type == EntityType::Url)
			|| (entity.type == EntityType::Email);
		const auto full = plainUrl
			? QStringView(entity.data).mid(0, entity.data.size())
			: inText;
		const auto customTextLink = (entity.type == EntityType::CustomUrl);
		const auto internalLink = customTextLink
			&& entity.data.startsWith(qstr("internal:"));
		if (composeExpanded) {
			const auto sameAsTextLink = customTextLink
				&& (entity.data
					== UrlClickHandler::EncodeForOpening(full.toString()));
			if (customTextLink && !internalLink && !sameAsTextLink) {
				const auto &url = entity.data;
				result.expanded.append(qstr(" (")).append(url).append(')');
			}
		}
		if (composeEntities && !internalLink) {
			insertEntity({
				entity.type,
				linkStart,
				int(result.rich.text.size() - linkStart),
				plainUrl ? QString() : entity.data });
		}
	};
	const auto appendPartCallback = [&](
			QStringView part,
			const QString &customEmojiData) {
		result.rich.text += part;
		if (composeExpanded) {
			result.expanded += part;
		}
		if (composeEntities && !customEmojiData.isEmpty()) {
			insertEntity({
				EntityType::CustomEmoji,
				int(result.rich.text.size() - part.size()),
				int(part.size()),
				customEmojiData,
			});
		}
	};

	enumerateText(
		selection,
		appendPartCallback,
		clickHandlerStartCallback,
		clickHandlerFinishCallback,
		flagsChangeCallback);

	if (composeEntities) {
		const auto proj = [](const EntityInText &entity) {
			const auto type = entity.type();
			const auto isUrl = (type == EntityType::Url)
				|| (type == EntityType::CustomUrl)
				|| (type == EntityType::BotCommand)
				|| (type == EntityType::Mention)
				|| (type == EntityType::MentionName)
				|| (type == EntityType::Hashtag)
				|| (type == EntityType::Cashtag);
			return std::pair{ entity.offset(), isUrl ? 0 : 1 };
		};
		const auto pred = [&](const EntityInText &a, const EntityInText &b) {
			return proj(a) < proj(b);
		};
		std::sort(
			result.rich.entities.begin(),
			result.rich.entities.end(),
			pred);
	}

	return result;
}

IsolatedEmoji String::toIsolatedEmoji() const {
	auto result = IsolatedEmoji();
	const auto skip = (_blocks.empty()
		|| _blocks.back()->type() != TextBlockTSkip) ? 0 : 1;
	if ((_blocks.size() > kIsolatedEmojiLimit + skip)
		|| !_spoilers.empty()) {
		return {};
	}
	auto index = 0;
	for (const auto &block : _blocks) {
		const auto type = block->type();
		if (block->lnkIndex()) {
			return {};
		} else if (type == TextBlockTEmoji) {
			result.items[index++] = block.unsafe<EmojiBlock>()._emoji;
		} else if (type == TextBlockTCustomEmoji) {
			result.items[index++]
				= block.unsafe<CustomEmojiBlock>()._custom->entityData();
		} else if (type != TextBlockTSkip) {
			return {};
		}
	}
	return result;
}

void String::clear() {
	clearFields();
	_text.clear();
}

void String::clearFields() {
	_blocks.clear();
	_links.clear();
	_spoilers.clear();
	_maxWidth = _minHeight = 0;
	_startDir = Qt::LayoutDirectionAuto;
}

ClickHandlerPtr String::spoilerLink(uint16 spoilerIndex) const {
	if (spoilerIndex) {
		const auto &handler = _spoilers.at(spoilerIndex - 1);
		return (handler && !handler->shown()) ? handler : nullptr;
	}
	return nullptr;
}

bool IsWordSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::Space:
	case QChar::LineFeed:
	case '.':
	case ',':
	case '?':
	case '!':
	case '@':
	case '#':
	case '$':
	case ':':
	case ';':
	case '-':
	case '<':
	case '>':
	case '[':
	case ']':
	case '(':
	case ')':
	case '{':
	case '}':
	case '=':
	case '/':
	case '+':
	case '%':
	case '&':
	case '^':
	case '*':
	case '\'':
	case '"':
	case '`':
	case '~':
	case '|':
		return true;
	default:
		break;
	}
	return false;
}

bool IsAlmostLinkEnd(QChar ch) {
	switch (ch.unicode()) {
	case '?':
	case ',':
	case '.':
	case '"':
	case ':':
	case '!':
	case '\'':
		return true;
	default:
		break;
	}
	return false;
}

bool IsLinkEnd(QChar ch) {
	return IsBad(ch)
		|| IsSpace(ch)
		|| IsNewline(ch)
		|| ch.isLowSurrogate()
		|| ch.isHighSurrogate();
}

bool IsNewline(QChar ch) {
	return (ch == QChar::LineFeed)
		|| (ch == 156);
}

bool IsSpace(QChar ch) {
	return ch.isSpace()
		|| (ch < 32)
		|| (ch == QChar::ParagraphSeparator)
		|| (ch == QChar::LineSeparator)
		|| (ch == QChar::ObjectReplacementCharacter)
		|| (ch == QChar::CarriageReturn)
		|| (ch == QChar::Tabulation)
		|| (ch == QChar(8203)/*Zero width space.*/);
}

bool IsDiac(QChar ch) { // diac and variation selectors
	return (ch.category() == QChar::Mark_NonSpacing)
		|| (ch == 1652)
		|| (ch >= 64606 && ch <= 64611);
}

bool IsReplacedBySpace(QChar ch) {
	// \xe2\x80[\xa8 - \xac\xad] // 8232 - 8237
	// QString from1 = QString::fromUtf8("\xe2\x80\xa8"), to1 = QString::fromUtf8("\xe2\x80\xad");
	// \xcc[\xb3\xbf\x8a] // 819, 831, 778
	// QString bad1 = QString::fromUtf8("\xcc\xb3"), bad2 = QString::fromUtf8("\xcc\xbf"), bad3 = QString::fromUtf8("\xcc\x8a");
	// [\x00\x01\x02\x07\x08\x0b-\x1f] // '\t' = 0x09
	return (/*code >= 0x00 && */ch <= 0x02)
		|| (ch >= 0x07 && ch <= 0x09)
		|| (ch >= 0x0b && ch <= 0x1f)
		|| (ch == 819)
		|| (ch == 831)
		|| (ch == 778)
		|| (ch >= 8232 && ch <= 8237);
}

bool IsTrimmed(QChar ch) {
	return (IsSpace(ch) || IsBad(ch));
}

} // namespace Text
} // namespace Ui
