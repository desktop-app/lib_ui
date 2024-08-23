// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_block_parser.h"

#include "base/platform/base_platform_info.h"
#include "ui/integration.h"
#include "ui/text/text_extended_data.h"
#include "ui/text/text_isolated_emoji.h"
#include "styles/style_basic.h"

#include <QtCore/QUrl>
#include <private/qfixed_p.h>

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
	const bool parseColorized = (options.flags & TextParseColorized);
	if (!preparsed.isEmpty() && (parseLinks || parseColorized)) {
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
					(!parseLinks
						&& (type == EntityType::Url
							|| type == EntityType::CustomUrl)) ||
					(type == EntityType::BotCommand && !parseBotCommands) || // #TODO entities
					(!parseMarkdown && (type == EntityType::Bold
						|| type == EntityType::Semibold
						|| type == EntityType::Italic
						|| type == EntityType::Underline
						|| type == EntityType::StrikeOut
						|| type == EntityType::Colorized
						|| type == EntityType::Spoiler
						|| type == EntityType::Code
						|| type == EntityType::Pre
						|| type == EntityType::Blockquote))) {
					continue;
				}
				result.entities.push_back(preparsed.at(i));
			}
		}
	}
	return result;
}

// Tilde fix in OpenSans.
[[nodiscard]] bool ComputeCheckTilde(const style::TextStyle &st) {
	const auto &font = st.font;
	return (font->size() * style::DevicePixelRatio() == 13)
		&& (font->flags() == 0)
		&& (font->f.family() == qstr("Open Sans"));
}

[[nodiscard]] bool IsDiacriticAllowedAfter(QChar ch) {
	const auto code = ch.unicode();
	const auto category = ch.category();
	return (code > 32)
		&& (category != QChar::Other_Control)
		&& (category != QChar::Other_Format)
		&& (category != QChar::Other_PrivateUse)
		&& (category != QChar::Other_NotAssigned);
}

} // namespace

BlockParser::StartedEntity::StartedEntity(TextBlockFlags flags)
: _value(flags.value())
, _type(Type::Flags) {
	Expects(_value >= 0 && _value < int(kStringLinkIndexShift));
}

BlockParser::StartedEntity::StartedEntity(uint16 index, Type type)
: _value(index)
, _type(type) {
	Expects((_type == Type::Link)
		? (_value >= kStringLinkIndexShift)
		: (_value < kStringLinkIndexShift));
}

BlockParser::StartedEntity::Type BlockParser::StartedEntity::type() const {
	return _type;
}

std::optional<TextBlockFlags> BlockParser::StartedEntity::flags() const {
	if (_value < int(kStringLinkIndexShift) && (_type == Type::Flags)) {
		return TextBlockFlags::from_raw(uint16(_value));
	}
	return std::nullopt;
}

std::optional<uint16> BlockParser::StartedEntity::linkIndex() const {
	if ((_value < int(kStringLinkIndexShift) && (_type == Type::IndexedLink))
		|| (_value >= int(kStringLinkIndexShift) && (_type == Type::Link))) {
		return uint16(_value);
	}
	return std::nullopt;
}

std::optional<uint16> BlockParser::StartedEntity::colorIndex() const {
	if (_type == Type::Colorized) {
		return uint16(_value);
	}
	return std::nullopt;
}

BlockParser::BlockParser(
	not_null<String*> string,
	const TextWithEntities &textWithEntities,
	const TextParseOptions &options,
	const std::any &context)
: BlockParser(
	string,
	PrepareRichFromRich(textWithEntities, options),
	options,
	context,
	ReadyToken()) {
}

BlockParser::BlockParser(
	not_null<String*> string,
	TextWithEntities &&source,
	const TextParseOptions &options,
	const std::any &context,
	ReadyToken)
: _t(string)
, _tText(string->_text)
, _tBlocks(string->_blocks)
, _source(std::move(source))
, _context(context)
, _start(_source.text.constData())
, _end(_start + _source.text.size())
, _ptr(_start)
, _entitiesEnd(_source.entities.end())
, _waitingEntity(_source.entities.begin())
, _multiline(options.flags & TextParseMultiline)
, _checkTilde(ComputeCheckTilde(*_t->_st)) {
	parse(options);
}

void BlockParser::createBlock(int skipBack) {
	if (_linkIndex < kStringLinkIndexShift && _linkIndex > _maxLinkIndex) {
		_maxLinkIndex = _linkIndex;
	}
	if (_linkIndex > kStringLinkIndexShift) {
		_maxShiftedLinkIndex = std::max(
			uint16(_linkIndex - kStringLinkIndexShift),
			_maxShiftedLinkIndex);
	}

	const auto length = int(_tText.size()) + skipBack - _blockStart;
	if (length <= 0) {
		return;
	}
	const auto newline = !_emoji
		&& (length == 1)
		&& (_tText.at(_blockStart) == QChar::LineFeed);
	if (_newlineAwaited) {
		_newlineAwaited = false;
		if (!newline) {
			_t->insertModifications(_blockStart, 1);
			_tText.insert(_blockStart, QChar::LineFeed);
			createBlock(skipBack - length);
		}
	}
	const auto linkIndex = _monoIndex ? _monoIndex : _linkIndex;
	auto custom = _customEmojiData.isEmpty()
		? nullptr
		: Integration::Instance().createCustomEmoji(
			_customEmojiData,
			_context);
	const auto push = [&](auto &&factory, auto &&...args) {
		_tBlocks.push_back(factory({
			.position = uint16(_blockStart),
			.flags = _flags,
			.linkIndex = linkIndex,
			.colorIndex = _colorIndex,
		}, std::forward<decltype(args)>(args)...));
	};
	if (custom) {
		push(&Block::CustomEmoji, std::move(custom));
	} else if (_emoji) {
		push(&Block::Emoji, _emoji);
	} else if (newline) {
		push(&Block::Newline, _quoteIndex);
	} else {
		push(&Block::Text/*, _t->_minResizeWidth*/);
	}
	// Diacritic can't attach from the next block to this one.
	_allowDiacritic = false;
	_blockStart += length;
	_customEmojiData = QByteArray();
	_emoji = nullptr;
}

void BlockParser::createNewlineBlock(bool fromOriginalText) {
	if (!fromOriginalText) {
		_t->insertModifications(_tText.size(), 1);
	}
	_tText.push_back(QChar::LineFeed);
	_allowDiacritic = false;
	createBlock();
}

void BlockParser::ensureAtNewline(QuoteDetails quote) {
	createBlock();
	const auto lastType = _tBlocks.empty()
		? TextBlockType::Newline
		: _tBlocks.back()->type();
	if (lastType != TextBlockType::Newline) {
		auto saved = base::take(_customEmojiData);
		createNewlineBlock(false);
		_customEmojiData = base::take(saved);
	}
	_quoteStartPosition = _tText.size();
	auto &quotes = _t->ensureQuotes()->list;
	quotes.push_back(std::move(quote));
	const auto index = _quoteIndex = int(quotes.size());
	if (_tBlocks.empty()) {
		_t->_startQuoteIndex = index;
	} else {
		auto &last = _tBlocks.back();
		Assert(last->type() == TextBlockType::Newline);
		last.unsafe<NewlineBlock>().setQuoteIndex(index);
	}
}

void BlockParser::finishEntities() {
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
					const auto lastType = _tBlocks.empty()
						? TextBlockType::Newline
						: _tBlocks.back()->type();
					if ((*flags)
						& (TextBlockFlag::Pre | TextBlockFlag::Blockquote)) {
						if (_quoteIndex) {
							auto &quotes = _t->ensureQuotes()->list;
							auto &quote = quotes[_quoteIndex - 1];
							const auto from = _quoteStartPosition;
							const auto till = _tText.size();
							if (quote.pre && till > from) {
								quote.copy = std::make_shared<PreClickHandler>(
									_t,
									from,
									till - from);
							} else if (quote.blockquote && quote.collapsed) {
								quote.toggle = std::make_shared<BlockquoteClickHandler>(
									_t,
									_quoteIndex);
							}
						}
						_quoteIndex = 0;
						if (lastType != TextBlockType::Newline) {
							_newlineAwaited = true;
						} else if (_tBlocks.empty()) {
							_t->_startQuoteIndex = 0;
						} else {
							auto &last = _tBlocks.back();
							last.unsafe<NewlineBlock>().setQuoteIndex(0);
						}
					}
					if (IsMono(*flags)) {
						_monoIndex = 0;
					}
				}
			} else if (const auto linkIndex = list.back().linkIndex()) {
				if (_linkIndex == *linkIndex) {
					createBlock();
					_linkIndex = 0;
				}
			} else if (const auto colorIndex = list.back().colorIndex()) {
				if (_colorIndex == *colorIndex) {
					createBlock();
					_colorIndex = 0;
				}
			}
			list.pop_back();
		}
	}
}

// Returns true if at least one entity was parsed in the current position.
bool BlockParser::checkEntities() {
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
		flags = TextBlockFlag::Bold;
	} else if (entityType == EntityType::Semibold) {
		flags = TextBlockFlag::Semibold;
	} else if (entityType == EntityType::Italic) {
		flags = TextBlockFlag::Italic;
	} else if (entityType == EntityType::Underline) {
		flags = TextBlockFlag::Underline;
	} else if (entityType == EntityType::Spoiler) {
		flags = TextBlockFlag::Spoiler;
	} else if (entityType == EntityType::StrikeOut) {
		flags = TextBlockFlag::StrikeOut;
	} else if ((entityType == EntityType::Code) // #TODO entities
		|| (entityType == EntityType::Pre)) {
		if (entityType == EntityType::Code) {
			flags = TextBlockFlag::Code;
		} else {
			flags = TextBlockFlag::Pre;
			ensureAtNewline({
				.language = _waitingEntity->data(),
				.pre = true,
			});
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
	} else if (entityType == EntityType::Blockquote) {
		flags = TextBlockFlag::Blockquote;
		ensureAtNewline({
			.blockquote = true,
			.collapsed = !_waitingEntity->data().isEmpty(),
		});
	} else if (entityType == EntityType::Url
		|| entityType == EntityType::Email
		|| entityType == EntityType::Phone
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
	} else if (entityType == EntityType::Colorized) {
		createBlock();

		const auto data = _waitingEntity->data();
		_colorIndex = data.isEmpty() ? 1 : (data.front().unicode() + 1);
		_startedEntities[entityEnd].emplace_back(
			_colorIndex,
			Type::Colorized);
	}

	if (link.type != EntityType::Invalid) {
		createBlock();

		_links.push_back(link);
		const auto tempIndex = _links.size();
		const auto useCustom = processCustomIndex(tempIndex);
		_linkIndex = tempIndex + (useCustom ? 0 : kStringLinkIndexShift);
		_startedEntities[entityEnd].emplace_back(
			_linkIndex,
			useCustom ? Type::IndexedLink : Type::Link);
	} else if (flags) {
		if (!(_flags & flags)) {
			createBlock();
			_flags |= flags;
			_startedEntities[entityEnd].emplace_back(flags);
			_monoIndex = monoIndex;
		}
	}

	++_waitingEntity;
	skipBadEntities();
	return true;
}

bool BlockParser::processCustomIndex(uint16 index) {
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

void BlockParser::skipPassedEntities() {
	while (_waitingEntity != _entitiesEnd
		&& _start + _waitingEntity->offset() + _waitingEntity->length() <= _ptr) {
		++_waitingEntity;
	}
}

void BlockParser::skipBadEntities() {
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

void BlockParser::parseCurrentChar() {
	_ch = ((_ptr < _end) ? *_ptr : 0);
	_emojiLookback = 0;
	const auto inCustomEmoji = !_customEmojiData.isEmpty();
	const auto isNewLine = !inCustomEmoji && _multiline && IsNewline(_ch);
	const auto replaceWithSpace = IsSpace(_ch) && (_ch != QChar::Nbsp);
	const auto isDiacritic = IsDiacritic(_ch);
	const auto isTilde = !inCustomEmoji && _checkTilde && (_ch == '~');
	const auto skip = [&] {
		if (IsBad(_ch) || _ch.isLowSurrogate()) {
			return true;
		} else if (_ch == 0xFE0F && Platform::IsMac()) {
			// Some sequences like 0x0E53 0xFE0F crash OS X harfbuzz text processing :(
			return true;
		} else if (isDiacritic) {
			if (!_allowDiacritic
				|| _emoji
				|| ++_diacritics > kMaxDiacAfterSymbol) {
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
		_tText.push_back(_ch);
		++_ptr;
		_ch = *_ptr;
		_emojiLookback = 1;
	}

	if (skip) {
		if (_ptr < _end) {
			_t->insertModifications(_tText.size(), -1);
		}
		_ch = 0;
		_allowDiacritic = false;
	} else {
		if (isTilde) { // Tilde fix in OpenSans.
			if (!(_flags & TextBlockFlag::Tilde)) {
				createBlock(-_emojiLookback);
				_flags |= TextBlockFlag::Tilde;
			}
		} else {
			if (_flags & TextBlockFlag::Tilde) {
				createBlock(-_emojiLookback);
				_flags &= ~TextBlockFlag::Tilde;
			}
		}
		if (isNewLine) {
			createBlock();
			createNewlineBlock(true);
		} else if (replaceWithSpace) {
			_tText.push_back(QChar::Space);
			_allowDiacritic = false;
		} else {
			if (_emoji) {
				createBlock(-_emojiLookback);
			}
			_tText.push_back(_ch);
			_allowDiacritic = IsDiacriticAllowedAfter(_ch);
		}
		if (!isDiacritic) {
			_diacritics = 0;
		}
	}
}

void BlockParser::parseEmojiFromCurrent() {
	if (!_customEmojiData.isEmpty()) {
		return;
	}
	int len = 0;
	auto e = Emoji::Find(_ptr - _emojiLookback, _end, &len);
	if (!e) return;

	for (int l = len - _emojiLookback - 1; l > 0; --l) {
		_tText.push_back(*++_ptr);
	}
	if (e->hasPostfix()) {
		Assert(!_tText.isEmpty());
		const auto last = _tText[_tText.size() - 1];
		if (last.unicode() != Emoji::kPostfix) {
			_t->insertModifications(_tText.size(), 1);
			_tText.push_back(QChar(Emoji::kPostfix));
			++len;
		}
	}

	createBlock(-len);
	_emoji = e;
}

bool BlockParser::isInvalidEntity(const EntityInText &entity) const {
	const auto length = entity.length();
	return (_start + entity.offset() + length > _end) || (length <= 0);
}

bool BlockParser::isLinkEntity(const EntityInText &entity) const {
	const auto type = entity.type();
	const auto urls = {
		EntityType::Url,
		EntityType::CustomUrl,
		EntityType::Email,
		EntityType::Hashtag,
		EntityType::Cashtag,
		EntityType::Mention,
		EntityType::MentionName,
		EntityType::Phone,
		EntityType::BotCommand
	};
	return ranges::find(urls, type) != std::end(urls);
}

void BlockParser::parse(const TextParseOptions &options) {
	skipBadEntities();
	trimSourceRange();

	_tText.resize(0);
	if (_t->_extended) {
		base::take(_t->_extended->modifications);
	}
	_tText.reserve(_end - _ptr);

	if (_ptr > _start) {
		_t->insertModifications(0, -(_ptr - _start));
	}

	for (; _ptr <= _end; ++_ptr) {
		while (checkEntities()) {
		}
		parseCurrentChar();
		parseEmojiFromCurrent();

		if (_tText.size() >= 0x8000) {
			break; // 32k max
		}
	}
	createBlock();
	finalize(options);
}

void BlockParser::trimSourceRange() {
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

// void BlockParser::checkForElidedSkipBlock() {
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

void BlockParser::finalize(const TextParseOptions &options) {
	auto links = (_maxLinkIndex || _maxShiftedLinkIndex)
		? &_t->ensureExtended()->links
		: nullptr;
	if (links) {
		links->resize(_maxLinkIndex + _maxShiftedLinkIndex);
	}
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
	const auto length = int(_tText.size());
	for (auto &block : _tBlocks) {
		if (block->type() == TextBlockType::CustomEmoji) {
			_t->_hasCustomEmoji = true;
		} else if (block->type() != TextBlockType::Newline
			&& block->type() != TextBlockType::Skip) {
			_t->_isOnlyCustomEmoji = false;
		} else if (block->linkIndex()) {
			_t->_isOnlyCustomEmoji = _t->_isIsolatedEmoji = false;
		}
		if (!_t->_hasNotEmojiAndSpaces) {
			if (block->type() == TextBlockType::Text) {
				if (spacesCheckFrom == uint16(-1)) {
					spacesCheckFrom = block->position();
				}
			} else if (spacesCheckFrom != uint16(-1)) {
				const auto checkTill = block->position();
				for (auto i = spacesCheckFrom; i != checkTill; ++i) {
					Assert(i < length);
					if (!_tText[i].isSpace()) {
						_t->_hasNotEmojiAndSpaces = true;
						break;
					}
				}
				spacesCheckFrom = uint16(-1);
			}
		}
		if (_t->_isIsolatedEmoji) {
			if (block->type() == TextBlockType::CustomEmoji
				|| block->type() == TextBlockType::Emoji) {
				if (++isolatedEmojiCount > kIsolatedEmojiLimit) {
					_t->_isIsolatedEmoji = false;
				}
			} else if (block->type() != TextBlockType::Skip) {
				_t->_isIsolatedEmoji = false;
			}
		}
		if (block->flags() & TextBlockFlag::Spoiler) {
			auto &spoiler = _t->ensureExtended()->spoiler;
			if (!spoiler) {
				spoiler = std::make_unique<SpoilerData>(
					Integration::Instance().createSpoilerRepaint(_context));
			}
		}
		const auto shiftedIndex = block->linkIndex();
		auto useCustomIndex = false;
		if (shiftedIndex <= kStringLinkIndexShift) {
			if (IsMono(block->flags()) && shiftedIndex) {
				const auto monoIndex = shiftedIndex;

				if (lastHandlerIndex.mono == monoIndex) {
					block->setLinkIndex(currentIndex);
					continue; // Optimization.
				} else {
					currentIndex++;
				}
				avoidIntersectionsWithCustom();
				block->setLinkIndex(currentIndex);
				const auto handler = Integration::Instance().createLinkHandler(
					_monos[monoIndex - 1],
					_context);
				if (!links) {
					links = &_t->ensureExtended()->links;
				}
				links->resize(currentIndex);
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
			block->setLinkIndex(usedIndex());
			continue; // Optimization.
		} else {
			(useCustomIndex ? counterCustomIndex : currentIndex)++;
		}
		if (!useCustomIndex) {
			avoidIntersectionsWithCustom();
		}
		block->setLinkIndex(usedIndex());

		if (links) {
			links->resize(std::max(usedIndex(), uint16(links->size())));
		}
		const auto handler = Integration::Instance().createLinkHandler(
			_links[realIndex - 1],
			_context);
		if (handler) {
			_t->setLink(usedIndex(), handler);
		}
		lastHandlerIndex.lnk = realIndex;
	}
	const auto hasSpoiler = (_t->_extended && _t->_extended->spoiler);
	if (!_t->_hasCustomEmoji || hasSpoiler) {
		_t->_isOnlyCustomEmoji = false;
	}
	if (_tBlocks.empty() || hasSpoiler) {
		_t->_isIsolatedEmoji = false;
	}
	if (!_t->_hasNotEmojiAndSpaces && spacesCheckFrom != uint16(-1)) {
		Assert(spacesCheckFrom < length);
		for (auto i = spacesCheckFrom; i != length; ++i) {
			Assert(i < length);
			if (!_tText[i].isSpace()) {
				_t->_hasNotEmojiAndSpaces = true;
				break;
			}
		}
	}
	_tText.squeeze();
	_tBlocks.shrink_to_fit();
	if (const auto extended = _t->_extended.get()) {
		extended->links.shrink_to_fit();
		extended->modifications.shrink_to_fit();
	}
}

void BlockParser::computeLinkText(
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
