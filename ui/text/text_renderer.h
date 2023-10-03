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

struct FixedRange {
	QFixed from;
	QFixed till;

	[[nodiscard]] bool empty() const {
		return (till <= from);
	}
};

[[nodiscard]] FixedRange Intersected(FixedRange a, FixedRange b);
[[nodiscard]] bool Intersects(FixedRange a, FixedRange b);
[[nodiscard]] FixedRange United(FixedRange a, FixedRange b);
[[nodiscard]] bool Distinct(FixedRange a, FixedRange b);

class Renderer final {
public:
	explicit Renderer(const Ui::Text::String &t);
	~Renderer();

	void draw(QPainter &p, const PaintContext &context);
	[[nodiscard]] StateResult getState(
		QPoint point,
		GeometryDescriptor geometry,
		StateRequest request);

private:
	static constexpr int kSpoilersRectsSize = 512;

	struct BidiControl;

	void enumerate();

	[[nodiscard]] crl::time now() const;
	void initNextParagraph(
		String::TextBlocks::const_iterator i,
		Qt::LayoutDirection direction);
	void initNextLine();
	void initParagraphBidi();
	bool drawLine(
		uint16 _lineEnd,
		const String::TextBlocks::const_iterator &_endBlockIter,
		const String::TextBlocks::const_iterator &_end);
	void fillSelectRange(FixedRange range);
	void pushSpoilerRange(
		FixedRange range,
		FixedRange selected,
		bool isElidedItem);
	void fillSpoilerRects();
	void fillSpoilerRects(
		QVarLengthArray<QRect, kSpoilersRectsSize> &rects,
		QVarLengthArray<FixedRange> &ranges);
	void paintSpoilerRects();
	void paintSpoilerRects(
		const QVarLengthArray<QRect, kSpoilersRectsSize> &rects,
		const style::color &color,
		int index);
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
	[[nodiscard]] ClickHandlerPtr lookupLink(
		const AbstractBlock *block) const;

	const String *_t = nullptr;
	GeometryDescriptor _geometry;
	SpoilerData *_spoiler = nullptr;
	SpoilerMessCache *_spoilerCache = nullptr;
	QPainter *_p = nullptr;
	const style::TextPalette *_palette = nullptr;
	std::span<SpecialColor> _colors;
	bool _pausedEmoji = false;
	bool _pausedSpoiler = false;
	style::align _align = style::al_topleft;
	QPen _originalPen;
	QPen _originalPenSelected;
	const QPen *_currentPen = nullptr;
	const QPen *_currentPenSelected = nullptr;
	struct {
		bool spoiler = false;
		bool selectActiveBlock = false; // For monospace.
	} _background;
	int _yFrom = 0;
	int _yTo = 0;
	TextSelection _selection = { 0, 0 };
	bool _fullWidthSelection = true;
	const QChar *_str = nullptr;
	mutable crl::time _cachedNow = 0;
	float64 _spoilerOpacity = 0.;
	QVarLengthArray<FixedRange> _spoilerRanges;
	QVarLengthArray<FixedRange> _spoilerSelectedRanges;
	QVarLengthArray<QRect, kSpoilersRectsSize> _spoilerRects;
	QVarLengthArray<QRect, kSpoilersRectsSize> _spoilerSelectedRects;

	std::optional<CustomEmoji::Context> _customEmojiContext;
	int _customEmojiSize = 0;
	int _customEmojiSkip = 0;
	int _indexOfElidedBlock = -1; // For spoilers.

	// current paragraph data
	String::TextBlocks::const_iterator _parStartBlock;
	Qt::LayoutDirection _parDirection = Qt::LayoutDirectionAuto;
	int _parStart = 0;
	int _parLength = 0;
	bool _parHasBidi = false;
	QVarLengthArray<QScriptAnalysis, 4096> _parAnalysis;

	// current line data
	QTextEngine *_e = nullptr;
	style::font _f;
	int _startLeft = 0;
	int _startTop = 0;
	QFixed _x, _wLeft, _last_rPadding;
	int _y = 0;
	int _yDelta = 0;
	int _lineHeight = 0;
	int _fontHeight = 0;
	bool _breakEverywhere = false;
	bool _elidedLine = false;

	// elided hack support
	int _blocksSize = 0;
	int _elideSavedIndex = 0;
	std::optional<Block> _elideSavedBlock;

	int _lineStart = 0;
	int _localFrom = 0;
	int _lineStartBlock = 0;
	QFixed _lineWidth = 0;
	QFixed _paragraphWidthRemaining = 0;

	// link and symbol resolve
	QFixed _lookupX = 0;
	int _lookupY = 0;
	bool _lookupSymbol = false;
	bool _lookupLink = false;
	StateRequest _lookupRequest;
	StateResult _lookupResult;

};

} // namespace Ui::Text
