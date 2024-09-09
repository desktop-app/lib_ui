// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_word_parser.h"

#include "ui/text/text_bidi_algorithm.h"
#include "styles/style_basic.h"

// COPIED FROM qtextlayout.cpp AND MODIFIED
namespace Ui::Text {

glyph_t WordParser::LineBreakHelper::currentGlyph() const {
	Q_ASSERT(currentPosition > 0);
	Q_ASSERT(logClusters[currentPosition - 1] < glyphs.numGlyphs);

	return glyphs.glyphs[logClusters[currentPosition - 1]];
}

void WordParser::LineBreakHelper::saveCurrentGlyph() {
	if (currentPosition > 0
		&& logClusters[currentPosition - 1] < glyphs.numGlyphs) {
		// needed to calculate right bearing later
		previousGlyph = currentGlyph();
		previousGlyphFontEngine = fontEngine;
	} else {
		previousGlyph = 0;
		previousGlyphFontEngine = nullptr;
	}
}

void WordParser::LineBreakHelper::calculateRightBearing(
		QFontEngine *engine,
		glyph_t glyph) {
	qreal rb;
	engine->getGlyphBearings(glyph, 0, &rb);

	// We only care about negative right bearings, so we limit the range
	// of the bearing here so that we can assume it's negative in the rest
	// of the code, as well as use QFixed(1) as a sentinel to represent
	// the state where we have yet to compute the right bearing.
	rightBearing = qMin(QFixed::fromReal(rb), QFixed(0));
}

void WordParser::LineBreakHelper::calculateRightBearing() {
	if (currentPosition > 0
		&& logClusters[currentPosition - 1] < glyphs.numGlyphs
		&& !whiteSpaceOrObject) {
		calculateRightBearing(fontEngine.data(), currentGlyph());
	} else {
		rightBearing = 0;
	}
}

void WordParser::LineBreakHelper::calculateRightBearingForPreviousGlyph() {
	if (previousGlyph > 0) {
		calculateRightBearing(previousGlyphFontEngine.data(), previousGlyph);
	} else {
		rightBearing = 0;
	}
}

// We always calculate the right bearing right before it is needed.
// So we don't need caching / optimizations referred to delayed right bearing calculations.

//static const QFixed RightBearingNotCalculated;

//inline void WordParser::LineBreakHelper::resetRightBearing()
//{
//	rightBearing = RightBearingNotCalculated;
//}

// We express the negative right bearing as an absolute number
// so that it can be applied to the width using addition.
QFixed WordParser::LineBreakHelper::negativeRightBearing() const {
	//if (rightBearing == RightBearingNotCalculated)
	//	return QFixed(0);

	return qAbs(rightBearing);
}

void WordParser::addNextCluster(
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

WordParser::BidiInitedAnalysis::BidiInitedAnalysis(not_null<String*> text)
: list(text->_text.size()) {
	BidiAlgorithm bidi(
		text->_text.constData(),
		list.data(),
		text->_text.size(),
		false, // baseDirectionIsRtl
		begin(text->_blocks),
		end(text->_blocks),
		0); // offsetInBlocks
	bidi.process();
}

WordParser::WordParser(not_null<String*> string)
: _t(string)
, _tText(_t->_text)
, _tBlocks(_t->_blocks)
, _tWords(_t->_words)
, _analysis(_t)
, _engine(_t, _analysis.list)
, _e(_engine.wrapped()) {
	parse();
}

void WordParser::parse() {
	_tWords.clear();
	if (_tText.isEmpty()) {
		return;
	}
	_newItem = _e.findItem(0);
	_attributes = _e.attributes();
	if (!_attributes) {
		return;
	}
	_lbh.logClusters = _e.layoutData->logClustersPtr;

	while (_newItem < _e.layoutData->items.size()) {
		if (_newItem != _item) {
			_attributes = moveToNewItemGetAttributes();
			if (!_attributes) {
				return;
			}
		}
		const auto &current = _e.layoutData->items[_item];
		const auto atSpaceBreak = [&] {
			for (auto index = _lbh.currentPosition; index < _itemEnd; ++index) {
				if (!_attributes[index].whiteSpace) {
					return false;
				} else if (isSpaceBreak(_attributes, index)) {
					return true;
				}
			}
			return false;
		}();
		if (current.analysis.flags == QScriptAnalysis::LineOrParagraphSeparator) {
			pushAccumulatedWord();
			processSingleGlyphItem();
			pushNewline(_wordStart, _engine.blockIndex(_wordStart));
			wordProcessed(_itemEnd);
		} else if (current.analysis.flags == QScriptAnalysis::Object) {
			pushAccumulatedWord();
			processSingleGlyphItem(current.width);
			_lbh.calculateRightBearing();
			pushFinishedWord(
				_wordStart,
				_lbh.tmpData.textWidth,
				-_lbh.negativeRightBearing());
			wordProcessed(_itemEnd);
		} else if (atSpaceBreak) {
			pushAccumulatedWord();
			accumulateWhitespaces();
			ensureWordForRightPadding();
			_tWords.back().add_rpadding(_lbh.spaceData.textWidth);
			wordProcessed(_lbh.currentPosition, true);
		} else {
			_lbh.whiteSpaceOrObject = false;
			do {
				addNextCluster(
					_lbh.currentPosition,
					_itemEnd,
					_lbh.tmpData,
					_lbh.glyphCount,
					current,
					_lbh.logClusters,
					_lbh.glyphs);

				if (_lbh.currentPosition >= _e.layoutData->string.length()
					|| isSpaceBreak(_attributes, _lbh.currentPosition)
					|| isLineBreak(_attributes, _lbh.currentPosition)) {
					maybeStartUnfinishedWord();
					_lbh.calculateRightBearing();
					pushFinishedWord(
						_wordStart,
						_lbh.tmpData.textWidth,
						-_lbh.negativeRightBearing());
					wordProcessed(_lbh.currentPosition);
					break;
				} else if (_attributes[_lbh.currentPosition].graphemeBoundary) {
					maybeStartUnfinishedWord();
					if (_addingEachGrapheme) {
						_lbh.calculateRightBearing();
						pushUnfinishedWord(
							_wordStart,
							_lbh.tmpData.textWidth,
							-_lbh.negativeRightBearing());
						wordContinued(_lbh.currentPosition);
					} else {
						_lastGraphemeBoundaryPosition = _lbh.currentPosition;
						_lastGraphemeBoundaryLine = _lbh.tmpData;
						_lbh.saveCurrentGlyph();
					}
				}
			} while (_lbh.currentPosition < _itemEnd);
		}
		if (_lbh.currentPosition == _itemEnd)
			_newItem = _item + 1;
	}
	if (!_tWords.empty()) {
		_tWords.shrink_to_fit();
	}
}

const QCharAttributes *WordParser::moveToNewItemGetAttributes() {
	_item = _newItem;
	auto &si = _e.layoutData->items[_item];
	auto result = _e.attributes();
	if (!si.num_glyphs) {
		_engine.shapeGetBlock(_item);
		result = _e.attributes();
		if (!result) {
			return nullptr;
		}
		_lbh.logClusters = _e.layoutData->logClustersPtr;
	}
	_lbh.currentPosition = si.position;
	_itemEnd = si.position + _e.length(_item);
	_lbh.glyphs = _e.shapedGlyphs(&si);
	const auto fontEngine = _e.fontEngine(si);
	if (_lbh.fontEngine != fontEngine) {
		_lbh.fontEngine = fontEngine;
	}
	return result;
}

void WordParser::pushAccumulatedWord() {
	if (_wordStart < _lbh.currentPosition) {
		_lbh.calculateRightBearing();
		pushFinishedWord(
			_wordStart,
			_lbh.tmpData.textWidth,
			-_lbh.negativeRightBearing());
		wordProcessed(_lbh.currentPosition);
	}
}

void WordParser::processSingleGlyphItem(QFixed added) {
	_lbh.whiteSpaceOrObject = true;
	++_lbh.tmpData.length;
	_lbh.tmpData.textWidth += added;

	_newItem = _item + 1;
	++_lbh.glyphCount;
}

void WordParser::wordProcessed(int nextWordStart, bool spaces) {
	wordContinued(nextWordStart, spaces);
	_addingEachGrapheme = false;
	_lastGraphemeBoundaryPosition = -1;
	_lastGraphemeBoundaryLine = ScriptLine();
}

void WordParser::wordContinued(int nextPartStart, bool spaces) {
	if (spaces) {
		_lbh.spaceData.textWidth = 0;
		_lbh.spaceData.length = 0;
	} else {
		_lbh.tmpData.textWidth = 0;
		_lbh.tmpData.length = 0;
	}
	_wordStart = nextPartStart;
}

void WordParser::accumulateWhitespaces() {
	const auto &current = _e.layoutData->items[_item];

	_lbh.whiteSpaceOrObject = true;
	while (_lbh.currentPosition < _itemEnd
		&& _attributes[_lbh.currentPosition].whiteSpace)
		addNextCluster(
			_lbh.currentPosition,
			_itemEnd,
			_lbh.spaceData,
			_lbh.glyphCount,
			current,
			_lbh.logClusters,
			_lbh.glyphs);
}

void WordParser::ensureWordForRightPadding() {
	if (_tWords.empty()) {
		_lbh.calculateRightBearing();
		pushFinishedWord(
			_wordStart,
			_lbh.tmpData.textWidth,
			-_lbh.negativeRightBearing());
	}
}

void WordParser::maybeStartUnfinishedWord() {
	if (!_addingEachGrapheme && _lbh.tmpData.textWidth > _t->_minResizeWidth) {
		if (_lastGraphemeBoundaryPosition >= 0) {
			_lbh.calculateRightBearingForPreviousGlyph();
			pushUnfinishedWord(
				_wordStart,
				_lastGraphemeBoundaryLine.textWidth,
				-_lbh.negativeRightBearing());
			_lbh.tmpData.textWidth -= _lastGraphemeBoundaryLine.textWidth;
			_lbh.tmpData.length -= _lastGraphemeBoundaryLine.length;
			_wordStart = _lastGraphemeBoundaryPosition;
		}
		_addingEachGrapheme = true;
	}
}

void WordParser::pushFinishedWord(
		uint16 position,
		QFixed width,
		QFixed rbearing) {
	const auto unfinished = false;
	_tWords.push_back(Word(position, unfinished, width, rbearing));
}

void WordParser::pushUnfinishedWord(
		uint16 position,
		QFixed width,
		QFixed rbearing) {
	const auto unfinished = true;
	_tWords.push_back(Word(position, unfinished, width, rbearing));
}

void WordParser::pushNewline(uint16 position, int newlineBlockIndex) {
	_tWords.push_back(Word(position, newlineBlockIndex));
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
