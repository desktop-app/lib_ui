// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"

#include <private/qtextengine_p.h>

struct QScriptAnalysis;
struct QScriptLine;

namespace Ui::Text {

class Renderer final {
public:
	explicit Renderer(const Ui::Text::String &t);
	~Renderer();

	void draw(QPainter &p, const PaintContext &context);
	[[nodiscard]] StateResult getState(
		QPoint point,
		int w,
		StateRequest request);
	[[nodiscard]] StateResult getStateElided(
		QPoint point,
		int w,
		StateRequestElided request);

private:
	struct BidiControl;

	void enumerate();

	[[nodiscard]] crl::time now() const;
	void initNextParagraph(String::TextBlocks::const_iterator i);
	void initParagraphBidi();
	bool drawLine(
		uint16 _lineEnd,
		const String::TextBlocks::const_iterator &_endBlockIter,
		const String::TextBlocks::const_iterator &_end);
	void fillSelectRange(QFixed from, QFixed to);
	[[nodiscard]] float64 fillSpoilerOpacity();
	void fillSpoilerRange(
		QFixed x,
		QFixed width,
		int currentBlockIndex,
		int positionFrom,
		int positionTill);
	void elideSaveBlock(
		int32 blockIndex,
		const AbstractBlock *&_endBlock,
		int32 elideStart,
		int32 elideWidth);
	void setElideBidi(int32 elideStart, int32 elideLen);
	void prepareElidedLine(
		QString &lineText,
		int32 lineStart,
		int32 &lineLength,
		const AbstractBlock *&_endBlock,
		int repeat = 0);
	void restoreAfterElided();

	// COPIED FROM qtextengine.cpp AND MODIFIED
	static void eAppendItems(
		QScriptAnalysis *analysis,
		int &start,
		int &stop,
		const BidiControl &control,
		QChar::Direction dir);
	void eShapeLine(const QScriptLine &line);
	[[nodiscard]] style::font applyFlags(int32 flags, const style::font &f);
	void eSetFont(const AbstractBlock *block);
	void eItemize();
	QChar::Direction eSkipBoundryNeutrals(
		QScriptAnalysis *analysis,
		const ushort *unicode,
		int &sor, int &eor, BidiControl &control,
		String::TextBlocks::const_iterator i);

	// creates the next QScript items.
	bool eBidiItemize(QScriptAnalysis *analysis, BidiControl &control);

	void applyBlockProperties(const AbstractBlock *block);

	const String *_t = nullptr;
	SpoilerMessCache *_spoilerCache = nullptr;
	QPainter *_p = nullptr;
	const style::TextPalette *_palette = nullptr;
	bool _elideLast = false;
	bool _breakEverywhere = false;
	int _elideRemoveFromEnd = 0;
	bool _paused = false;
	style::align _align = style::al_topleft;
	QPen _originalPen;
	QPen _originalPenSelected;
	const QPen *_currentPen = nullptr;
	const QPen *_currentPenSelected = nullptr;
	struct {
		const style::color *color = nullptr;
		bool inFront = false;
		crl::time startMs = 0;
		uint16 spoilerIndex = 0;

		bool selectActiveBlock = false; // For monospace.
	} _background;
	int _yFrom = 0;
	int _yTo = 0;
	int _yToElide = 0;
	TextSelection _selection = { 0, 0 };
	bool _fullWidthSelection = true;
	const QChar *_str = nullptr;
	mutable crl::time _cachedNow = 0;

	int _customEmojiSize = 0;
	int _customEmojiSkip = 0;
	int _indexOfElidedBlock = -1; // For spoilers.

	// current paragraph data
	String::TextBlocks::const_iterator _parStartBlock;
	Qt::LayoutDirection _parDirection;
	int _parStart = 0;
	int _parLength = 0;
	bool _parHasBidi = false;
	QVarLengthArray<QScriptAnalysis, 4096> _parAnalysis;

	// current line data
	QTextEngine *_e = nullptr;
	style::font _f;
	QFixed _x, _w, _wLeft, _last_rPadding;
	int _y = 0;
	int _yDelta = 0;
	int _lineHeight = 0;
	int _fontHeight = 0;

	// elided hack support
	int _blocksSize = 0;
	int _elideSavedIndex = 0;
	std::optional<Block> _elideSavedBlock;

	int _lineStart = 0;
	int _localFrom = 0;
	int _lineStartBlock = 0;

	// link and symbol resolve
	QFixed _lookupX = 0;
	int _lookupY = 0;
	bool _lookupSymbol = false;
	bool _lookupLink = false;
	StateRequest _lookupRequest;
	StateResult _lookupResult;

};

} // namespace Ui::Text
