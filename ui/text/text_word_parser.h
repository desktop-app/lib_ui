// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"
#include "ui/text/text_block.h"
#include "ui/text/text_stack_engine.h"
#include "ui/text/text_word.h"

namespace Ui::Text {

class WordParser {
public:
	explicit WordParser(not_null<String*> string);

private:
	struct BidiInitedAnalysis {
		explicit BidiInitedAnalysis(not_null<String*> text);

		QVarLengthArray<QScriptAnalysis, 4096> list;
	};

	void parse();

	void pushFinishedWord(uint16 position, QFixed width, QFixed rbearing);
	void pushUnfinishedWord(uint16 position, QFixed width, QFixed rbearing);
	void pushNewline(uint16 position, int newlineBlockIndex);

	[[nodiscard]] bool isLineBreak(
		const QCharAttributes *attributes,
		int index) const;
	[[nodiscard]] bool isSpaceBreak(
		const QCharAttributes *attributes,
		int index) const;

	const not_null<String*> _t;
	QString &_tText;
	std::vector<Block> &_tBlocks;
	std::vector<Word> &_tWords;
	BidiInitedAnalysis _analysis;
	StackEngine _engine;

};

} // namespace Ui::Text
