// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"
#include "ui/text/text_block.h"
#include "ui/text/text_custom_emoji.h"

#include <private/qtextengine_p.h>

class QTextItemInt;
struct QScriptAnalysis;
struct QScriptLine;
struct QScriptItem;

namespace Ui::Text {

inline constexpr auto kQuoteCollapsedLines = 3;

class AbstractBlock;

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
		Blocks::const_iterator i,
		int16 paragraphIndex,
		Qt::LayoutDirection direction);
	void initNextLine();
	void initParagraphBidi();
	bool drawLine(
		uint16 lineEnd,
		Blocks::const_iterator blocksEnd);
	[[nodiscard]] FixedRange findSelectEmojiRange(
		const QScriptItem &si,
		std::vector<Block>::const_iterator blockIt,
		QFixed x,
		TextSelection selection) const;
	[[nodiscard]] FixedRange findSelectTextRange(
		const QScriptItem &si,
		int itemStart,
		int itemEnd,
		QFixed x,
		QFixed itemWidth,
		const QTextItemInt &gf,
		TextSelection selection) const;
	void fillSelectRange(FixedRange range);
	void pushHighlightRange(FixedRange range);
	void pushSpoilerRange(
		FixedRange range,
		FixedRange selected,
		bool isElidedItem,
		bool rtl);
	void fillRectsFromRanges();
	void fillRectsFromRanges(
		QVarLengthArray<QRect, kSpoilersRectsSize> &rects,
		QVarLengthArray<FixedRange> &ranges);
	void paintSpoilerRects();
	void paintSpoilerRects(
		const QVarLengthArray<QRect, kSpoilersRectsSize> &rects,
		const style::color &color,
		int index);
	void composeHighlightPath();
	[[nodiscard]] const AbstractBlock *markBlockForElisionGetEnd(
		int blockIndex);
	void setElideBidi(int elideStart);
	void prepareElidedLine(
		QString &lineText,
		int lineStart,
		int &lineLength,
		const AbstractBlock *&endBlock,
		int recursed = 0);
	void prepareElisionAt(
		QString &lineText,
		int &lineLength,
		uint16 position);
	void restoreAfterElided();

	void fillParagraphBg(int paddingBottom);

	void applyBlockProperties(
		QTextEngine &e,
		not_null<const AbstractBlock*> block);
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
	QPen _quoteLinkPenOverride;
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
	HighlightInfoRequest *_highlight = nullptr;
	const QChar *_str = nullptr;
	mutable crl::time _cachedNow = 0;
	float64 _spoilerOpacity = 0.;
	QVarLengthArray<FixedRange> _spoilerRanges;
	QVarLengthArray<FixedRange> _spoilerSelectedRanges;
	QVarLengthArray<FixedRange> _highlightRanges;
	QVarLengthArray<QRect, kSpoilersRectsSize> _spoilerRects;
	QVarLengthArray<QRect, kSpoilersRectsSize> _spoilerSelectedRects;
	QVarLengthArray<QRect, kSpoilersRectsSize> _highlightRects;

	std::optional<CustomEmoji::Context> _customEmojiContext;
	int _customEmojiSkip = 0;
	int _indexOfElidedBlock = -1; // For spoilers.

	// current paragraph data
	Blocks::const_iterator _paragraphStartBlock;
	Qt::LayoutDirection _paragraphDirection = Qt::LayoutDirectionAuto;
	int _paragraphStart = 0;
	int _paragraphLength = 0;
	QVarLengthArray<QScriptAnalysis, 4096> _paragraphAnalysis;

	// current quote data
	QuoteDetails *_quote = nullptr;
	Qt::LayoutDirection _quoteDirection = Qt::LayoutDirectionAuto;
	int _quoteShift = 0;
	int _quoteIndex = 0;
	QMargins _quotePadding;
	int _quoteLinesLeft = -1;
	int _quoteTop = 0;
	int _quoteLineTop = 0;
	QuotePaintCache *_quotePreCache = nullptr;
	QuotePaintCache *_quoteBlockquoteCache = nullptr;
	bool _quotePreValid = false;
	bool _quoteBlockquoteValid = false;

	ClickHandlerPtr _quoteExpandLink;
	bool _quoteExpandLinkLookup = false;

	// current line data
	style::font _f;
	int _startLeft = 0;
	int _startTop = 0;
	int _startLineWidth = 0;
	QFixed _x, _wLeft, _last_rPadding;
	int _y = 0;
	int _yDelta = 0;
	int _lineIndex = 0;
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
	QFixed _lineStartPadding = 0;
	QFixed _lineWidth = 0;

	// link and symbol resolve
	QFixed _lookupX = 0;
	int _lookupY = 0;
	bool _lookupSymbol = false;
	bool _lookupLink = false;
	StateRequest _lookupRequest;
	StateResult _lookupResult;

};

} // namespace Ui::Text
