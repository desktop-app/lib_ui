// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_word_parser.h"

#include "styles/style_basic.h"

// COPIED FROM qtextlayout.cpp AND MODIFIED
namespace Ui::Text {
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

bool BlockParser::isLineBreak(
		const QCharAttributes *attributes,
		int index) const {
	// Don't break by '/' or '.' in the middle of the word.
	// In case of a line break or white space it'll allow break anyway.
	return attributes[index].lineBreak
		&& (index <= 0
			|| (text[index - 1] != '/' && text[index - 1] != '.'));
}

bool BlockParser::isSpaceBreak(
		const QCharAttributes *attributes,
		int index) const {
	// Don't break on &nbsp;
	return attributes[index].whiteSpace && (text[index] != QChar::Nbsp);
}

WordParser::WordParser(not_null<String*> string)
: _t(string)
, _tText(_t->_text)
, _tBlocks(_t->_blocks)
, _tWords(_t->_words)
, _engine(_tText, _t->_st->font->f) {
	parse();
}

void WordParser::parse() {
	if (!_tText.isEmpty()) {
		return;
	}
	QStackTextEngine engine(_tText, _t->_st->font->f);

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

	_tWords.clear();

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
				block._words.push_back(Word(
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
					block._words.push_back(Word(
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
							block._words.push_back(Word(
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
						block._words.push_back(Word(
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

void WordParser::pushWord(
		uint16 position,
		bool newline,
		bool continuation,
		QFixed width,
		QFixed rbearing,
		QFixed rpadding) {
	_tWords.push_back(
		Word(position, newline, continuation, width, rbearing, rpadding));
}

void WordParser::pushEmojiWord(int length, CustomEmoji *custom) {
	const auto position = _blockStart;
	const auto width = custom
		? custom->width()
		: (st::emojiSize + 2 * st::emojiPadding);
	const auto newline = false;
	const auto continuation = false;
	const auto rbearing = 0;
	auto rpadding = 0;
	for (auto i = length; i != 0;) {
		auto ch = _tText[position + (--i)];
		if (ch.unicode() == QChar::Space) {
			rpadding += _t->_st->font->spacew;
		} else {
			break;
		}
	}
	pushWord(position, newline, continuation, width, rbearing, rpadding);
}

void WordParser::pushNewlineWord(int length) {
	Expects(length == 1);

	const auto position = _blockStart;
	const auto width = 0;
	const auto newline = true;
	const auto continuation = false;
	const auto rbearing = 0;
	auto rpadding = 0;
	for (auto i = length; i != 0;) {
		auto ch = _tText[position + (--i)];
		if (ch.unicode() == QChar::Space) {
			rpadding += _t->_st->font->spacew;
		} else {
			break;
		}
	}
	pushWord(position, newline, continuation, width, rbearing, rpadding);
}


} // namespace Ui::Text
