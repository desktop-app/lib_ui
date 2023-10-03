// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_block.h"

#include "styles/style_basic.h"
#include "base/debug_log.h"

#include <private/qfontengine_p.h>

// COPIED FROM qtextlayout.cpp AND MODIFIED
namespace Ui {
namespace Text {
namespace {

struct ScriptLine {
	int length = 0;
	QFixed textWidth;
};

// All members finished with "_" are internal.
struct LineBreakHelper {
	ScriptLine tmpData;
	ScriptLine spaceData;

	QGlyphLayout glyphs;

	int glyphCount = 0;
	int maxGlyphs = INT_MAX;
	int currentPosition = 0;

	glyph_t previousGlyph_ = 0;
	QFontEngine *previousFontEngine_ = nullptr;

	QFixed rightBearing;

	QFontEngine *fontEngine = nullptr;
	const unsigned short *logClusters = nullptr;

	glyph_t currentGlyph() const;
	void saveCurrentGlyph();
	void calculateRightBearing(QFontEngine *engine, glyph_t glyph);
	void calculateRightBearing();
	void calculateRightBearingForPreviousGlyph();

	// We always calculate the right bearing right before it is needed.
	// So we don't need caching / optimizations referred to
	// delayed right bearing calculations.

	//static const QFixed RightBearingNotCalculated;

	//inline void resetRightBearing()
	//{
	//	rightBearing = RightBearingNotCalculated;
	//}

	// We express the negative right bearing as an absolute number
	// so that it can be applied to the width using addition.
	QFixed negativeRightBearing() const;

};

//const QFixed LineBreakHelper::RightBearingNotCalculated = QFixed(1);

glyph_t LineBreakHelper::currentGlyph() const {
	Q_ASSERT(currentPosition > 0);
	Q_ASSERT(logClusters[currentPosition - 1] < glyphs.numGlyphs);

	return glyphs.glyphs[logClusters[currentPosition - 1]];
}

void LineBreakHelper::saveCurrentGlyph() {
	if (currentPosition > 0
		&& logClusters[currentPosition - 1] < glyphs.numGlyphs) {
		// needed to calculate right bearing later
		previousGlyph_ = currentGlyph();
		previousFontEngine_ = fontEngine;
	} else {
		previousGlyph_ = 0;
		previousFontEngine_ = nullptr;
	}
}

void LineBreakHelper::calculateRightBearing(
		QFontEngine *engine,
		glyph_t glyph) {
	qreal rb;
	engine->getGlyphBearings(glyph, 0, &rb);

	// We only care about negative right bearings, so we limit the range
	// of the bearing here so that we can assume it's negative in the rest
	// of the code, as well ase use QFixed(1) as a sentinel to represent
	// the state where we have yet to compute the right bearing.
	rightBearing = qMin(QFixed::fromReal(rb), QFixed(0));
}

void LineBreakHelper::calculateRightBearing() {
	if (currentPosition > 0
		&& logClusters[currentPosition - 1] < glyphs.numGlyphs) {
		calculateRightBearing(fontEngine, currentGlyph());
	} else {
		rightBearing = 0;
	}
}

void LineBreakHelper::calculateRightBearingForPreviousGlyph() {
	if (previousGlyph_ > 0) {
		calculateRightBearing(previousFontEngine_, previousGlyph_);
	} else {
		rightBearing = 0;
	}
}

// We always calculate the right bearing right before it is needed.
// So we don't need caching / optimizations referred to delayed right bearing calculations.

//static const QFixed RightBearingNotCalculated;

//inline void resetRightBearing()
//{
//	rightBearing = RightBearingNotCalculated;
//}

// We express the negative right bearing as an absolute number
// so that it can be applied to the width using addition.
QFixed LineBreakHelper::negativeRightBearing() const {
	//if (rightBearing == RightBearingNotCalculated)
	//	return QFixed(0);

	return qAbs(rightBearing);
}

QString DebugCurrentParsingString, DebugCurrentParsingPart;
int DebugCurrentParsingFrom = 0;
int DebugCurrentParsingLength = 0;

void addNextCluster(
		int &pos,
		int end,
		ScriptLine &line,
		int &glyphCount,
		const QScriptItem &current,
		const unsigned short *logClusters,
		const QGlyphLayout &glyphs) {
	int glyphPosition = logClusters[pos];
	do { // got to the first next cluster
		++pos;
		++line.length;
	} while (pos < end && logClusters[pos] == glyphPosition);
	do { // calculate the textWidth for the rest of the current cluster.
		if (!glyphs.attributes[glyphPosition].dontPrint)
			line.textWidth += glyphs.advances[glyphPosition];
		++glyphPosition;
	} while (glyphPosition < current.num_glyphs
		&& !glyphs.attributes[glyphPosition].clusterStart);

	Q_ASSERT((pos == end && glyphPosition == current.num_glyphs)
		|| logClusters[pos] == glyphPosition);

	++glyphCount;
}

} // anonymous namespace

class BlockParser {
public:
	BlockParser(
		TextBlock &block,
		QTextEngine &engine,
		QFixed minResizeWidth,
		int blockPosition,
		const QString &text);

private:
	void parseWords(QFixed minResizeWidth, int blockFrom);
	[[nodiscard]] bool isLineBreak(
		const QCharAttributes *attributes,
		int index) const;
	[[nodiscard]] bool isSpaceBreak(
		const QCharAttributes *attributes,
		int index) const;

	TextBlock &block;
	QTextEngine &engine;
	const QString &text;

};

BlockParser::BlockParser(
	TextBlock &block,
	QTextEngine &engine,
	QFixed minResizeWidth,
	int blockFrom,
	const QString &text)
: block(block)
, engine(engine)
, text(text) {
	parseWords(minResizeWidth, blockFrom);
}

void BlockParser::parseWords(QFixed minResizeWidth, int blockFrom) {
	LineBreakHelper lbh;

	// Helper for debugging crashes in text processing.
	//
	//		auto debugChars = QString();
	//		debugChars.reserve(str.size() * 7);
	//		for (const auto ch : str) {
	//			debugChars.append(
	//				"0x").append(
	//				QString::number(ch.unicode(), 16).toUpper()).append(
	//				' ');
	//		}
	//		LOG(("Text: %1, chars: %2").arg(str).arg(debugChars));

	int item = -1;
	int newItem = engine.findItem(0);

	const QCharAttributes *attributes = engine.attributes();
	if (!attributes)
		return;
	int end = 0;
	lbh.logClusters = engine.layoutData->logClustersPtr;

	block._words.clear();

	int wordStart = lbh.currentPosition;

	bool addingEachGrapheme = false;
	int lastGraphemeBoundaryPosition = -1;
	ScriptLine lastGraphemeBoundaryLine;

	while (newItem < engine.layoutData->items.size()) {
		if (newItem != item) {
			item = newItem;
			const QScriptItem &current = engine.layoutData->items[item];
			if (!current.num_glyphs) {
				engine.shape(item);
				attributes = engine.attributes();
				if (!attributes)
					return;
				lbh.logClusters = engine.layoutData->logClustersPtr;
			}
			lbh.currentPosition = current.position;
			end = current.position + engine.length(item);
			lbh.glyphs = engine.shapedGlyphs(&current);
			QFontEngine *fontEngine = engine.fontEngine(current);
			if (lbh.fontEngine != fontEngine) {
				lbh.fontEngine = fontEngine;
			}
		}
		const QScriptItem &current = engine.layoutData->items[item];

		const auto atSpaceBreak = [&] {
			for (auto index = lbh.currentPosition; index < end; ++index) {
				if (!attributes[index].whiteSpace) {
					return false;
				} else if (isSpaceBreak(attributes, index)) {
					return true;
				}
			}
			return false;
		}();
		if (atSpaceBreak) {
			while (lbh.currentPosition < end
				&& attributes[lbh.currentPosition].whiteSpace)
				addNextCluster(
					lbh.currentPosition,
					end,
					lbh.spaceData,
					lbh.glyphCount,
					current,
					lbh.logClusters,
					lbh.glyphs);

			if (block._words.isEmpty()) {
				block._words.push_back(TextWord(
					wordStart + blockFrom,
					lbh.tmpData.textWidth,
					-lbh.negativeRightBearing()));
			}
			block._words.back().add_rpadding(lbh.spaceData.textWidth);
			block._width += lbh.spaceData.textWidth;
			lbh.spaceData.length = 0;
			lbh.spaceData.textWidth = 0;

			wordStart = lbh.currentPosition;

			addingEachGrapheme = false;
			lastGraphemeBoundaryPosition = -1;
			lastGraphemeBoundaryLine = ScriptLine();
		} else {
			do {
				addNextCluster(
					lbh.currentPosition,
					end,
					lbh.tmpData,
					lbh.glyphCount,
					current,
					lbh.logClusters,
					lbh.glyphs);

				if (lbh.currentPosition >= engine.layoutData->string.length()
					|| isSpaceBreak(attributes, lbh.currentPosition)
					|| isLineBreak(attributes, lbh.currentPosition)) {
					lbh.calculateRightBearing();
					block._words.push_back(TextWord(
						wordStart + blockFrom,
						lbh.tmpData.textWidth,
						-lbh.negativeRightBearing()));
					block._width += lbh.tmpData.textWidth;
					lbh.tmpData.textWidth = 0;
					lbh.tmpData.length = 0;
					wordStart = lbh.currentPosition;
					break;
				} else if (attributes[lbh.currentPosition].graphemeBoundary) {
					if (!addingEachGrapheme && lbh.tmpData.textWidth > minResizeWidth) {
						if (lastGraphemeBoundaryPosition >= 0) {
							lbh.calculateRightBearingForPreviousGlyph();
							block._words.push_back(TextWord(
								wordStart + blockFrom,
								-lastGraphemeBoundaryLine.textWidth,
								-lbh.negativeRightBearing()));
							block._width += lastGraphemeBoundaryLine.textWidth;
							lbh.tmpData.textWidth -= lastGraphemeBoundaryLine.textWidth;
							lbh.tmpData.length -= lastGraphemeBoundaryLine.length;
							wordStart = lastGraphemeBoundaryPosition;
						}
						addingEachGrapheme = true;
					}
					if (addingEachGrapheme) {
						lbh.calculateRightBearing();
						block._words.push_back(TextWord(
							wordStart + blockFrom,
							-lbh.tmpData.textWidth,
							-lbh.negativeRightBearing()));
						block._width += lbh.tmpData.textWidth;
						lbh.tmpData.textWidth = 0;
						lbh.tmpData.length = 0;
						wordStart = lbh.currentPosition;
					} else {
						lastGraphemeBoundaryPosition = lbh.currentPosition;
						lastGraphemeBoundaryLine = lbh.tmpData;
						lbh.saveCurrentGlyph();
					}
				}
			} while (lbh.currentPosition < end);
		}
		if (lbh.currentPosition == end)
			newItem = item + 1;
	}
	if (!block._words.isEmpty()) {
		block._rpadding = block._words.back().f_rpadding();
		block._width -= block._rpadding;
		block._words.squeeze();
	}
}

bool BlockParser::isLineBreak(
		const QCharAttributes *attributes,
		int index) const {
	// Don't break after / in links.
	return attributes[index].lineBreak
		&& (block.linkIndex() <= 0 || index <= 0 || text[index - 1] != '/');
}

bool BlockParser::isSpaceBreak(
		const QCharAttributes *attributes,
		int index) const {
	// Don't break on &nbsp;
	return attributes[index].whiteSpace && (text[index] != QChar::Nbsp);
}

style::font WithFlags(
		const style::font &font,
		TextBlockFlags flags,
		uint32 fontFlags) {
	using namespace style::internal;

	if (!flags && !fontFlags) {
		return font;
	} else if (IsMono(flags) || (fontFlags & FontMonospace)) {
		return font->monospace();
	}
	auto result = font;
	if ((flags & TextBlockFlag::Bold) || (fontFlags & FontBold)) {
		result = result->bold();
	} else if ((flags & TextBlockFlag::Semibold)
		|| (fontFlags & FontSemibold)) {
		result = result->semibold();
	}
	if ((flags & TextBlockFlag::Italic) || (fontFlags & FontItalic)) {
		result = result->italic();
	}
	if ((flags & TextBlockFlag::Underline) || (fontFlags & FontUnderline)) {
		result = result->underline();
	}
	if ((flags & TextBlockFlag::StrikeOut) || (fontFlags & FontStrikeOut)) {
		result = result->strikeout();
	}
	if (flags & TextBlockFlag::Tilde) { // Tilde fix in OpenSans.
		result = result->semibold();
	}
	return result;
}

AbstractBlock::AbstractBlock(
	const style::font &font,
	const QString &text,
	uint16 position,
	uint16 length,
	TextBlockType type,
	uint16 flags,
	uint16 linkIndex,
	uint16 colorIndex)
: _position(position)
, _type(static_cast<uint16>(type))
, _flags(flags)
, _linkIndex(linkIndex)
, _colorIndex(colorIndex) {
}

uint16 AbstractBlock::position() const {
	return _position;
}

TextBlockType AbstractBlock::type() const {
	return static_cast<TextBlockType>(_type);
}

TextBlockFlags AbstractBlock::flags() const {
	return TextBlockFlags::from_raw(_flags);
}

QFixed AbstractBlock::f_width() const {
	return _width;
}

QFixed AbstractBlock::f_rpadding() const {
	return _rpadding;
}

uint16 AbstractBlock::linkIndex() const {
	return _linkIndex;
}

uint16 AbstractBlock::colorIndex() const {
	return _colorIndex;
}

void AbstractBlock::setLinkIndex(uint16 index) {
	_linkIndex = index;
}

QFixed AbstractBlock::f_rbearing() const {
	return (type() == TextBlockType::Text)
		? static_cast<const TextBlock*>(this)->real_f_rbearing()
		: 0;
}

TextBlock::TextBlock(
	const style::font &font,
	const QString &text,
	uint16 position,
	uint16 length,
	TextBlockFlags flags,
	uint16 linkIndex,
	uint16 colorIndex,
	QFixed minResizeWidth)
: AbstractBlock(
		font,
		text,
		position,
		length,
		TextBlockType::Text,
		flags,
		linkIndex,
		colorIndex) {
	if (!length) {
		return;
	}
	const auto blockFont = WithFlags(font, flags);
	if (!flags && linkIndex) {
		// should use TextStyle lnkFlags somehow... not supported
	}

	const auto part = text.mid(_position, length);

	QStackTextEngine engine(part, blockFont->f);
	BlockParser parser(*this, engine, minResizeWidth, _position, part);
}

QFixed TextBlock::real_f_rbearing() const {
	return _words.isEmpty() ? 0 : _words.back().f_rbearing();
}

EmojiBlock::EmojiBlock(
	const style::font &font,
	const QString &text,
	uint16 position,
	uint16 length,
	TextBlockFlags flags,
	uint16 linkIndex,
	uint16 colorIndex,
	EmojiPtr emoji)
: AbstractBlock(
	font,
	text,
	position,
	length,
	TextBlockType::Emoji,
	flags,
	linkIndex,
	colorIndex)
, _emoji(emoji) {
	_width = int(st::emojiSize + 2 * st::emojiPadding);
	_rpadding = 0;
	for (auto i = length; i != 0;) {
		auto ch = text[_position + (--i)];
		if (ch.unicode() == QChar::Space) {
			_rpadding += font->spacew;
		} else {
			break;
		}
	}
}

CustomEmojiBlock::CustomEmojiBlock(
	const style::font &font,
	const QString &text,
	uint16 position,
	uint16 length,
	TextBlockFlags flags,
	uint16 linkIndex,
	uint16 colorIndex,
	std::unique_ptr<CustomEmoji> custom)
: AbstractBlock(
	font,
	text,
	position,
	length,
	TextBlockType::CustomEmoji,
	flags,
	linkIndex,
	colorIndex)
, _custom(std::move(custom)) {
	_width = int(st::emojiSize + 2 * st::emojiPadding);
	_rpadding = 0;
	for (auto i = length; i != 0;) {
		auto ch = text[_position + (--i)];
		if (ch.unicode() == QChar::Space) {
			_rpadding += font->spacew;
		} else {
			break;
		}
	}
}

NewlineBlock::NewlineBlock(
	const style::font &font,
	const QString &text,
	uint16 position,
	uint16 length,
	TextBlockFlags flags,
	uint16 linkIndex,
	uint16 colorIndex)
: AbstractBlock(
	font,
	text,
	position,
	length,
	TextBlockType::Newline,
	flags,
	linkIndex,
	colorIndex) {
}

Qt::LayoutDirection NewlineBlock::nextDirection() const {
	return _nextDirection;
}

SkipBlock::SkipBlock(
	const style::font &font,
	const QString &text,
	uint16 position,
	int32 width,
	int32 height,
	uint16 linkIndex,
	uint16 colorIndex)
: AbstractBlock(
	font,
	text,
	position,
	1,
	TextBlockType::Skip,
	0,
	linkIndex,
	colorIndex)
, _height(height) {
	_width = width;
}

int SkipBlock::height() const {
	return _height;
}


TextWord::TextWord(
	uint16 position,
	QFixed width,
	QFixed rbearing,
	QFixed rpadding)
: _position(position)
, _rbearing((rbearing.value() > 0x7FFF)
	? 0x7FFF
	: (rbearing.value() < -0x7FFF ? -0x7FFF : rbearing.value()))
, _width(width)
, _rpadding(rpadding) {
}

uint16 TextWord::position() const {
	return _position;
}

QFixed TextWord::f_rbearing() const {
	return QFixed::fromFixed(_rbearing);
}

QFixed TextWord::f_width() const {
	return _width;
}

QFixed TextWord::f_rpadding() const {
	return _rpadding;
}

void TextWord::add_rpadding(QFixed padding) {
	_rpadding += padding;
}

Block::Block() {
	Unexpected("Should not be called.");
}

Block::Block(Block &&other) {
	switch (other->type()) {
	case TextBlockType::Newline:
		emplace<NewlineBlock>(std::move(other.unsafe<NewlineBlock>()));
		break;
	case TextBlockType::Text:
		emplace<TextBlock>(std::move(other.unsafe<TextBlock>()));
		break;
	case TextBlockType::Emoji:
		emplace<EmojiBlock>(std::move(other.unsafe<EmojiBlock>()));
		break;
	case TextBlockType::CustomEmoji:
		emplace<CustomEmojiBlock>(
			std::move(other.unsafe<CustomEmojiBlock>()));
		break;
	case TextBlockType::Skip:
		emplace<SkipBlock>(std::move(other.unsafe<SkipBlock>()));
		break;
	default:
		Unexpected("Bad text block type in Block(Block&&).");
	}
}

Block &Block::operator=(Block &&other) {
	if (&other == this) {
		return *this;
	}
	destroy();
	switch (other->type()) {
	case TextBlockType::Newline:
		emplace<NewlineBlock>(std::move(other.unsafe<NewlineBlock>()));
		break;
	case TextBlockType::Text:
		emplace<TextBlock>(std::move(other.unsafe<TextBlock>()));
		break;
	case TextBlockType::Emoji:
		emplace<EmojiBlock>(std::move(other.unsafe<EmojiBlock>()));
		break;
	case TextBlockType::CustomEmoji:
		emplace<CustomEmojiBlock>(
			std::move(other.unsafe<CustomEmojiBlock>()));
		break;
	case TextBlockType::Skip:
		emplace<SkipBlock>(std::move(other.unsafe<SkipBlock>()));
		break;
	default:
		Unexpected("Bad text block type in operator=(Block&&).");
	}
	return *this;
}

Block::~Block() {
	destroy();
}

Block Block::Newline(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex) {
	return New<NewlineBlock>(
		font,
		text,
		position,
		length,
		flags,
		linkIndex,
		colorIndex);
}

Block Block::Text(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		QFixed minResizeWidth) {
	return New<TextBlock>(
		font,
		text,
		position,
		length,
		flags,
		linkIndex,
		colorIndex,
		minResizeWidth);
}

Block Block::Emoji(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		EmojiPtr emoji) {
	return New<EmojiBlock>(
		font,
		text,
		position,
		length,
		flags,
		linkIndex,
		colorIndex,
		emoji);
}

Block Block::CustomEmoji(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		std::unique_ptr<Text::CustomEmoji> custom) {
	return New<CustomEmojiBlock>(
		font,
		text,
		position,
		length,
		flags,
		linkIndex,
		colorIndex,
		std::move(custom));
}

Block Block::Skip(
		const style::font &font,
		const QString &text,
		uint16 position,
		int32 width,
		int32 height,
		uint16 linkIndex,
		uint16 colorIndex) {
	return New<SkipBlock>(
		font,
		text,
		position,
		width,
		height,
		linkIndex,
		colorIndex);
}

AbstractBlock *Block::get() {
	return &unsafe<AbstractBlock>();
}

const AbstractBlock *Block::get() const {
	return &unsafe<AbstractBlock>();
}

AbstractBlock *Block::operator->() {
	return get();
}

const AbstractBlock *Block::operator->() const {
	return get();
}

AbstractBlock &Block::operator*() {
	return *get();
}

const AbstractBlock &Block::operator*() const {
	return *get();
}

void Block::destroy() {
	switch (get()->type()) {
	case TextBlockType::Newline:
		unsafe<NewlineBlock>().~NewlineBlock();
		break;
	case TextBlockType::Text:
		unsafe<TextBlock>().~TextBlock();
		break;
	case TextBlockType::Emoji:
		unsafe<EmojiBlock>().~EmojiBlock();
		break;
	case TextBlockType::CustomEmoji:
		unsafe<CustomEmojiBlock>().~CustomEmojiBlock();
		break;
	case TextBlockType::Skip:
		unsafe<SkipBlock>().~SkipBlock();
		break;
	default:
		Unexpected("Bad text block type in Block(Block&&).");
	}
}

int CountBlockHeight(
		const AbstractBlock *block,
		const style::TextStyle *st) {
	return (block->type() == TextBlockType::Skip)
		? static_cast<const SkipBlock*>(block)->height()
		: (st->lineHeight > st->font->height)
		? st->lineHeight
		: st->font->height;
}

} // namespace Text
} // namespace Ui
