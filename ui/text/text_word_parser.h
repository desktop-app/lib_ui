// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"
#include "ui/text/text_block.h"
#include "ui/text/text_shaper.h"
#include "ui/text/text_word.h"

namespace Ui::Text {

class WordParser {
public:
	explicit WordParser(not_null<String*> string);

private:
	struct ScriptLine {
		int length = 0;
		Fixed textWidth;
	};
	struct LineBreakHelper {
		ScriptLine tmpData;
		ScriptLine spaceData;

		// The item being walked, and the one the last saved position was in,
		// which is not the same when a word spans items.
		ShapedItem shaped;
		ShapedItem previousShaped;
		int itemPosition = 0;
		int previousOffset = 0;

		int currentPosition = 0;

		Fixed rightBearing;

		bool whiteSpaceOrObject = true;

		void saveCurrentGlyph();
		void calculateRightBearing();
		void calculateRightBearingForPreviousGlyph();

		// We express the negative right bearing as an absolute number
		// so that it can be applied to the width using addition.
		[[nodiscard]] Fixed negativeRightBearing() const;

	};
	[[nodiscard]] static Paragraph ResolveParagraph(
		not_null<const String*> t);

	void parse();

	void moveToNewItem();

	void pushAccumulatedWord();
	void processSingleGlyphItem(Fixed added = 0);
	void wordProcessed(int nextWordStart, bool spaces = false);
	void wordContinued(int nextPartStart, bool spaces = false);
	void accumulateWhitespaces();
	void ensureWordForRightPadding();
	void maybeStartUnfinishedWord();
	void pushFinishedWord(uint16 position, Fixed width, Fixed rbearing);
	void pushUnfinishedWord(uint16 position, Fixed width, Fixed rbearing);
	void pushNewline(uint16 position, int newlineBlockIndex);

	void addNextCluster(int &pos, int end, ScriptLine &line);

	[[nodiscard]] bool isLineBreak(int index) const;
	[[nodiscard]] bool isSpaceBreak(int index) const;
	[[nodiscard]] bool clusterIsWhitespace(int index) const;

	const not_null<String*> _t;
	QString &_tText;
	std::vector<Block> &_tBlocks;
	std::vector<Word> &_tWords;
	Paragraph _paragraph;
	LineShaper _shaper;
	LineBreakHelper _lbh;
	gsl::span<const CharAttribute> _attributes;
	int _wordStart = 0;
	bool _addingEachGrapheme = false;
	int _lastGraphemeBoundaryPosition = -1;
	ScriptLine _lastGraphemeBoundaryLine;
	int _item = -1;
	int _newItem = -1;
	int _itemEnd = 0;

};

} // namespace Ui::Text
