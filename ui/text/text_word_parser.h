// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"
#include "ui/text/text_block.h"
#include "ui/text/text_word.h"

#include <private/qfontengine_p.h>

namespace Ui::Text {

class WordParser {
public:
	explicit WordParser(not_null<String*> string);

private:
	void parse();

	void pushWord(
		uint16 position,
		bool newline,
		bool continuation,
		QFixed width,
		QFixed rbearing,
		QFixed rpadding);
	void pushEmojiWord(int length, CustomEmoji *custom = nullptr);
	void pushNewlineWord(int length);

	const not_null<String*> _t;
	QString &_tText;
	std::vector<Block> &_tBlocks;
	std::vector<Word> &_tWords;
	QStackTextEngine _engine;

};

} // namespace Ui::Text
