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

WordParser::WordParser(not_null<String*> string)
: _t(string)
, _tText(_t->_text)
, _tBlocks(_t->_blocks)
, _tWords(_t->_words)
, _analysis(_tText.size())
, _engine(_t, _analysis) {
	parse();
}

void WordParser::parse() {
	if (!_tText.isEmpty()) {
		return;
	}

	_engine.itemize();
	auto &e = _engine.wrapped();

	LineBreakHelper lbh;

	int item = -1;
	int newItem = e.findItem(0);

	const QCharAttributes *attributes = e.attributes();
	if (!attributes)
		return;
	int end = 0;
	lbh.logClusters = e.layoutData->logClustersPtr;

	_tWords.clear();

	int wordStart = lbh.currentPosition;

	bool addingEachGrapheme = false;
	int lastGraphemeBoundaryPosition = -1;
	ScriptLine lastGraphemeBoundaryLine;

//void WordParser::pushNewlineWord(int length) {
//	Expects(length == 1);
//
//	const auto position = _blockStart;
//	const auto width = 0;
//	const auto newline = true;
//	const auto continuation = false;
//	const auto rbearing = 0;
//	auto rpadding = 0;
//	for (auto i = length; i != 0;) {
//		auto ch = _tText[position + (--i)];
//		if (ch.unicode() == QChar::Space) {
//			rpadding += _t->_st->font->spacew;
//		} else {
//			break;
//		}
//	}
//	pushWord(position, newline, continuation, width, rbearing, rpadding);
//}

	while (newItem < e.layoutData->items.size()) {
		if (newItem != item) {
			item = newItem;
			auto &si = e.layoutData->items[item];
			if (!si.num_glyphs) {
				const auto block = _engine.shapeGetBlock(item);
				attributes = e.attributes();
				if (!attributes)
					return;
				lbh.logClusters = e.layoutData->logClustersPtr;

				const auto type = block->type();
				if (si.analysis.flags == QScriptAnalysis::Object) {
					if (type == TextBlockType::Emoji) {
						si.width = st::emojiSize + 2 * st::emojiPadding;
					} else if (type == TextBlockType::CustomEmoji) {
						si.width = static_cast<const CustomEmojiBlock*>(
							block.get())->custom()->width();
					} else if (type == TextBlockType::Skip) {
						si.width = static_cast<const SkipBlock*>(
							block.get())->width();
						//si.width = currentBlock->f_width()
						//	+ (nextBlock == _endBlock && (!nextBlock || nextBlock->position() >= trimmedLineEnd)
						//		? 0
						//		: currentBlock->f_rpadding());
					}
				}

			}
			lbh.currentPosition = si.position;
			end = si.position + e.length(item);
			lbh.glyphs = e.shapedGlyphs(&si);
			QFontEngine *fontEngine = e.fontEngine(si);
			if (lbh.fontEngine != fontEngine) {
				lbh.fontEngine = fontEngine;
			}
		}
		const QScriptItem &current = e.layoutData->items[item];
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

			if (_tWords.empty()) {
				pushWord(
					wordStart,
					false, // newline
					false, // continuation
					lbh.tmpData.textWidth,
					-lbh.negativeRightBearing(),
					0); // rpadding
			}
			_tWords.back().add_rpadding(lbh.spaceData.textWidth);
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

				if (lbh.currentPosition >= e.layoutData->string.length()
					|| isSpaceBreak(attributes, lbh.currentPosition)
					|| isLineBreak(attributes, lbh.currentPosition)) {
					lbh.calculateRightBearing();
					pushWord(
						wordStart,
						false, // newline
						false, // continuation
						lbh.tmpData.textWidth,
						-lbh.negativeRightBearing(),
						0); // rpadding
					lbh.tmpData.textWidth = 0;
					lbh.tmpData.length = 0;
					wordStart = lbh.currentPosition;
					break;
				} else if (attributes[lbh.currentPosition].graphemeBoundary) {
					if (!addingEachGrapheme && lbh.tmpData.textWidth > _t->_minResizeWidth) {
						if (lastGraphemeBoundaryPosition >= 0) {
							lbh.calculateRightBearingForPreviousGlyph();
							pushWord(
								wordStart,
								false, // newline
								true, // continuation
								lastGraphemeBoundaryLine.textWidth,
								-lbh.negativeRightBearing(),
								0); // rpadding
							lbh.tmpData.textWidth -= lastGraphemeBoundaryLine.textWidth;
							lbh.tmpData.length -= lastGraphemeBoundaryLine.length;
							wordStart = lastGraphemeBoundaryPosition;
						}
						addingEachGrapheme = true;
					}
					if (addingEachGrapheme) {
						lbh.calculateRightBearing();
						pushWord(
							wordStart,
							false, // newline
							true, // continuation
							lbh.tmpData.textWidth,
							-lbh.negativeRightBearing(),
							0); // rpadding
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
	if (!_tWords.empty()) {
		_tWords.shrink_to_fit();
	}
}

void WordParser::pushWord(
		uint16 position,
		bool continuation,
		bool newline,
		QFixed width,
		QFixed rbearing,
		QFixed rpadding) {
	_tWords.push_back(
		Word(position, continuation, newline, width, rbearing, rpadding));
}

bool WordParser::isLineBreak(
		const QCharAttributes *attributes,
		int index) const {
	// Don't break by '/' or '.' in the middle of the word.
	// In case of a line break or white space it'll allow break anyway.
	return attributes[index].lineBreak
		&& (index <= 0
			|| (_tText[index - 1] != '/' && _tText[index - 1] != '.'));
}

bool WordParser::isSpaceBreak(
		const QCharAttributes *attributes,
		int index) const {
	// Don't break on &nbsp;
	return attributes[index].whiteSpace && (_tText[index] != QChar::Nbsp);
}

} // namespace Ui::Text
