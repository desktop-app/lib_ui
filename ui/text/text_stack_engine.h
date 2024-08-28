// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"

#include <private/qfontengine_p.h>

namespace Ui::Text {

class StackEngine final {
public:
	explicit StackEngine(
		not_null<const String*> t,
		gsl::span<QScriptAnalysis> analysis,
		int from = 0,
		int till = -1,
		int blockIndexHint = 0);
	explicit StackEngine(
		not_null<const String*> t,
		int offset,
		const QString &text,
		gsl::span<QScriptAnalysis> analysis,
		int blockIndexHint = 0,
		int blockIndexLimit = -1);

	[[nodiscard]] QTextEngine &wrapped() {
		return _engine;
	}

	void itemize();
	std::vector<Block>::const_iterator shapeGetBlock(int item);
	[[nodiscard]] int blockIndex(int position) const;

private:
	void updateFont(not_null<const AbstractBlock*> block);
	[[nodiscard]] std::vector<Block>::const_iterator adjustBlock(
		int offset) const;
	[[nodiscard]] int blockPosition(
		std::vector<Block>::const_iterator i) const;
	[[nodiscard]] int blockEnd(std::vector<Block>::const_iterator i) const;

	const not_null<const String*> _t;
	const QString &_text;
	QScriptAnalysis *_analysis = nullptr;
	const int _offset = 0;
	const int _positionEnd = 0;
	style::font _font;
	QStackTextEngine _engine;

	const std::vector<Block> &_tBlocks;
	std::vector<Block>::const_iterator _bStart;
	std::vector<Block>::const_iterator _bEnd;
	mutable std::vector<Block>::const_iterator _bCached;

};

} // namespace Ui::Text
