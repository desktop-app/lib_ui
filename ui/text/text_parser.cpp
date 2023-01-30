// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_parser.h"

#include "base/platform/base_platform_info.h"
#include "ui/integration.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/text_spoiler_data.h"
#include "styles/style_basic.h"

#include <QtCore/QUrl>

namespace Ui::Text {
namespace {

constexpr auto kStringLinkIndexShift = uint16(0x8000);
constexpr auto kMaxDiacAfterSymbol = 2;

[[nodiscard]] TextWithEntities PrepareRichFromRich(
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

[[nodiscard]] QFixed ComputeStopAfter(
		const TextParseOptions &options,
		const style::TextStyle &st) {
	return (options.maxw > 0 && options.maxh > 0)
		? ((options.maxh / st.font->height) + 1) * options.maxw
		: QFIXED_MAX;
}

// Open Sans tilde fix.
[[nodiscard]] bool ComputeCheckTilde(const style::TextStyle &st) {
	const auto &font = st.font;
	return (font->size() * style::DevicePixelRatio() == 13)
		&& (font->flags() == 0)
		&& (font->f.family() == qstr("DAOpenSansRegular"));
}

} // namespace

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
	auto isolatedEmojiCount = 0;
	_t->_hasCustomEmoji = false;
	_t->_isIsolatedEmoji = true;
	_t->_isOnlyCustomEmoji = true;
	_t->_hasNotEmojiAndSpaces = false;
	auto spacesCheckFrom = uint16(-1);
	const auto length = int(_t->_text.size());
	for (auto &block : _t->_blocks) {
		if (block->type() == TextBlockTCustomEmoji) {
			_t->_hasCustomEmoji = true;
		} else if (block->type() != TextBlockTNewline
			&& block->type() != TextBlockTSkip) {
			_t->_isOnlyCustomEmoji = false;
		} else if (block->lnkIndex()) {
			_t->_isOnlyCustomEmoji = _t->_isIsolatedEmoji = false;
		}
		if (!_t->_hasNotEmojiAndSpaces) {
			if (block->type() == TextBlockTText) {
				if (spacesCheckFrom == uint16(-1)) {
					spacesCheckFrom = block->from();
				}
			} else if (spacesCheckFrom != uint16(-1)) {
				const auto checkTill = block->from();
				for (auto i = spacesCheckFrom; i != checkTill; ++i) {
					Assert(i < length);
					if (!_t->_text[i].isSpace()) {
						_t->_hasNotEmojiAndSpaces = true;
						break;
					}
				}
				spacesCheckFrom = uint16(-1);
			}
		}
		if (_t->_isIsolatedEmoji) {
			if (block->type() == TextBlockTCustomEmoji
				|| block->type() == TextBlockTEmoji) {
				if (++isolatedEmojiCount > kIsolatedEmojiLimit) {
					_t->_isIsolatedEmoji = false;
				}
			} else if (block->type() != TextBlockTSkip) {
				_t->_isIsolatedEmoji = false;
			}
		}
		if (block->spoilerIndex()) {
			if (!_t->_spoiler.data) {
				_t->_spoiler.data = std::make_unique<SpoilerData>(
					Integration::Instance().createSpoilerRepaint(_context));
			}
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
	if (!_t->_hasCustomEmoji || _t->_spoiler.data) {
		_t->_isOnlyCustomEmoji = false;
	}
	if (_t->_blocks.empty() || _t->_spoiler.data) {
		_t->_isIsolatedEmoji = false;
	}
	if (!_t->_hasNotEmojiAndSpaces && spacesCheckFrom != uint16(-1)) {
		Assert(spacesCheckFrom < length);
		for (auto i = spacesCheckFrom; i != length; ++i) {
			Assert(i < length);
			if (!_t->_text[i].isSpace()) {
				_t->_hasNotEmojiAndSpaces = true;
				break;
			}
		}
	}
	_t->_links.squeeze();
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

} // namespace Ui::Text
