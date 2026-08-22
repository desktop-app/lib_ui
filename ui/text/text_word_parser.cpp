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

void WordParser::LineBreakHelper::saveCurrentGlyph() {
	// Needed to calculate the right bearing later.
	previousShaped = shaped;
	previousOffset = currentPosition - itemPosition;
}

void WordParser::LineBreakHelper::calculateRightBearing() {
	rightBearing = whiteSpaceOrObject
		? Fixed()
		: shaped.rightBearingBefore(currentPosition - itemPosition);
}

void WordParser::LineBreakHelper::calculateRightBearingForPreviousGlyph() {
	rightBearing = previousShaped
		? previousShaped.rightBearingBefore(previousOffset)
		: Fixed();
}

// We express the negative right bearing as an absolute number
// so that it can be applied to the width using addition.
Fixed WordParser::LineBreakHelper::negativeRightBearing() const {
	return abs(rightBearing);
}

void WordParser::addNextCluster(int &pos, int end, ScriptLine &line) {
	const auto from = pos - _lbh.itemPosition;
	const auto till = _lbh.shaped.clusterEnd(from, end - _lbh.itemPosition);
	line.length += (till - from);
	line.textWidth += _lbh.shaped.width(from, till);
	pos = till + _lbh.itemPosition;
}

Paragraph WordParser::ResolveParagraph(not_null<const String*> t) {
	auto result = Paragraph();
	result.resolve(
		t,
		0,
		int(t->_text.size()),
		false, // baseRtl
		0, // blockIndexHint
		-1); // blockIndexLimit
	return result;
}

WordParser::WordParser(not_null<String*> string)
: _t(string)
, _tText(_t->_text)
, _tBlocks(_t->_blocks)
, _tWords(_t->_words)
, _paragraph(ResolveParagraph(_t))
, _shaper(_t, _paragraph, 0, _tText) {
	parse();
}

void WordParser::parse() {
	_tWords.clear();
	if (_tText.isEmpty()) {
		return;
	}
	const auto lastItem = _shaper.findItem(int(_tText.size()) - 1);
	if (lastItem < 0) {
		return;
	}
	_shaper.shapeRange(0, lastItem);
	_attributes = _shaper.attributes();
	if (_attributes.empty()) {
		return;
	}
	const auto &items = _shaper.items();

	_newItem = 0;
	while (_newItem < int(items.size())) {
		if (_newItem != _item) {
			moveToNewItem();
		}
		const auto &current = items[_item];
		const auto atSpaceBreak = [&] {
			if (!clusterIsWhitespace(_lbh.currentPosition)) {
				return false;
			}
			for (auto index = _lbh.currentPosition; index < _itemEnd; ++index) {
				if (!_attributes[index].whiteSpace) {
					return false;
				} else if (isSpaceBreak(index)) {
					return true;
				}
			}
			return false;
		}();
		if (current.newline) {
			pushAccumulatedWord();
			processSingleGlyphItem();
			pushNewline(_wordStart, _shaper.blockIndex(_wordStart));
			wordProcessed(_itemEnd);
		} else if (current.object) {
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
					_lbh.tmpData);

				if (_lbh.currentPosition >= _tText.size()
					|| isSpaceBreak(_lbh.currentPosition)
					|| isLineBreak(_lbh.currentPosition)) {
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

void WordParser::moveToNewItem() {
	_item = _newItem;
	const auto &item = _shaper.items()[_item];
	_lbh.shaped = _shaper.shape(_item);
	_lbh.itemPosition = item.position;
	_lbh.currentPosition = item.position;
	_itemEnd = item.position + item.length;
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

void WordParser::processSingleGlyphItem(Fixed added) {
	_lbh.whiteSpaceOrObject = true;
	++_lbh.tmpData.length;
	_lbh.tmpData.textWidth += added;

	_newItem = _item + 1;
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
	_lbh.whiteSpaceOrObject = true;
	while (_lbh.currentPosition < _itemEnd
		&& _attributes[_lbh.currentPosition].whiteSpace
		&& clusterIsWhitespace(_lbh.currentPosition))
		addNextCluster(_lbh.currentPosition, _itemEnd, _lbh.spaceData);
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
		Fixed width,
		Fixed rbearing) {
	const auto unfinished = false;
	_tWords.push_back(Word(position, unfinished, width, rbearing));
}

void WordParser::pushUnfinishedWord(
		uint16 position,
		Fixed width,
		Fixed rbearing) {
	const auto unfinished = true;
	_tWords.push_back(Word(position, unfinished, width, rbearing));
}

void WordParser::pushNewline(uint16 position, int newlineBlockIndex) {
	_tWords.push_back(Word(position, newlineBlockIndex));
}

bool WordParser::isLineBreak(int index) const {
	// Don't break by '/' or '.' in the middle of the word.
	// In case of a line break or white space it'll allow break anyway.
	return _attributes[index].lineBreak
		&& (index <= 0
			|| (_tText[index - 1] != '/' && _tText[index - 1] != '.'));
}

bool WordParser::isSpaceBreak(int index) const {
	// Don't break on &nbsp;
	return _attributes[index].whiteSpace && (_tText[index] != QChar::Nbsp);
}

bool WordParser::clusterIsWhitespace(int index) const {
	// A mark with no letter to sit on is shaped onto the space before it, and
	// the two come out as one cluster, which can not be cut in half. Such a
	// cluster carries ink of its own, so it belongs to a word and not to the
	// padding a run of spaces makes: padding is dropped at the end of a line,
	// and the width the text reports would be short of what it draws.
	const auto from = index - _lbh.itemPosition;
	const auto till = _lbh.shaped.clusterEnd(from, _itemEnd - _lbh.itemPosition);
	for (auto i = from; i != till; ++i) {
		if (!_attributes[i + _lbh.itemPosition].whiteSpace) {
			return false;
		}
	}
	return true;
}

} // namespace Ui::Text
