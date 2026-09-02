// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_renderer.h"

#include "ui/text/text_bidi_algorithm.h"
#include "ui/text/text_block.h"
#include "ui/text/text_extended_data.h"
#include "ui/text/text_stack_engine.h"
#include "ui/text/text_word.h"
#include "ui/style/style_core.h"
#include "styles/style_basic.h"

namespace Ui::Text {
namespace {

constexpr auto kQuoteHeaderTextLarge = 25;

const ClickHandlerPtr &CustomEmojiMismatchLink() {
	static const ClickHandlerPtr result
		= std::make_shared<LambdaClickHandler>([] {});
	return result;
}

void InitTextItemWithScriptItem(QTextItemInt &ti, const QScriptItem &si) {
	// explicitly initialize flags so that initFontAttributes can be called
	// multiple times on the same TextItem
	ti.flags = { };
	if (si.analysis.bidiLevel % 2)
		ti.flags |= QTextItem::RightToLeft;
	ti.ascent = si.ascent;
	ti.descent = si.descent;

	if (ti.charFormat.hasProperty(QTextFormat::TextUnderlineStyle)) {
		ti.underlineStyle = ti.charFormat.underlineStyle();
	} else if (ti.charFormat.boolProperty(QTextFormat::FontUnderline)
		|| ti.f->underline()) {
		ti.underlineStyle = QTextCharFormat::SingleUnderline;
	}

	// compat
	if (ti.underlineStyle == QTextCharFormat::SingleUnderline)
		ti.flags |= QTextItem::Underline;

	if (ti.f->overline() || ti.charFormat.fontOverline())
		ti.flags |= QTextItem::Overline;
	if (ti.f->strikeOut() || ti.charFormat.fontStrikeOut())
		ti.flags |= QTextItem::StrikeOut;
}

void AppendRange(
		QVarLengthArray<FixedRange> &ranges,
		FixedRange range) {
	for (auto i = ranges.begin(); i != ranges.end(); ++i) {
		if (range.till < i->from) {
			ranges.insert(i, range);
			return;
		} else if (!Distinct(range, *i)) {
			*i = United(*i, range);
			for (auto j = i + 1; j != ranges.end(); ++j) {
				if (j->from > i->till) {
					ranges.erase(i + 1, j);
					return;
				} else {
					*i = United(*i, *j);
				}
			}
			ranges.erase(i + 1, ranges.end());
			return;
		}
	}
	ranges.push_back(range);
}

// A visible letter can take several characters - a consonant with its matra, a
// conjunct, a base with combining marks - and the only places a caret can be at
// are its boundaries, so that is where a cut or a click belongs.
[[nodiscard]] int GraphemeEnd(
		const QCharAttributes *attributes,
		int from,
		int till) {
	for (auto i = from + 1; i < till; ++i) {
		if (attributes[i].graphemeBoundary) {
			return i;
		}
	}
	return till;
}

// The characters that share one glyph: a ligature, a consonant with its matra, a
// base with its marks. Nothing inside one can be drawn or cut on its own.
[[nodiscard]] int ClusterEnd(
		const unsigned short *logClusters,
		int from,
		int till) {
	for (auto i = from + 1; i < till; ++i) {
		if (logClusters[i] != logClusters[from]) {
			return i;
		}
	}
	return till;
}

// Where a cut may fall: a letter boundary that is also a glyph boundary, since
// half a glyph cannot be drawn and half a letter should not be shown.
[[nodiscard]] int CutEnd(
		const QCharAttributes *attributes,
		const unsigned short *logClusters,
		int from,
		int till) {
	auto result = GraphemeEnd(attributes, from, till);
	while (result < till && logClusters[result] == logClusters[from]) {
		result = GraphemeEnd(attributes, result, till);
	}
	return result;
}

// A cluster can map to several glyphs, and the first one's advance is not the
// whole of it.
[[nodiscard]] QFixed GlyphsAdvance(
		const QGlyphLayout &glyphs,
		int from,
		int till) {
	auto result = QFixed();
	for (auto i = from; i < till; ++i) {
		result += glyphs.effectiveAdvance(i);
	}
	return result;
}

} // namespace

FixedRange Intersected(FixedRange a, FixedRange b) {
	return {
		.from = std::max(a.from, b.from),
		.till = std::min(a.till, b.till),
	};
}

bool Intersects(FixedRange a, FixedRange b) {
	return (a.till > b.from) && (b.till > a.from);
}

FixedRange United(FixedRange a, FixedRange b) {
	return {
		.from = std::min(a.from, b.from),
		.till = std::max(a.till, b.till),
	};
}

bool Distinct(FixedRange a, FixedRange b) {
	return (a.till < b.from) || (b.till < a.from);
}

Renderer::Renderer(const Ui::Text::String &t)
: _t(&t)
, _spoiler(_t->_extended ? _t->_extended->spoiler.get() : nullptr) {
}

Renderer::~Renderer() {
	restoreAfterElided();
	if (_p) {
		_p->setPen(_originalPen);
	}
}

void Renderer::draw(QPainter &p, const PaintContext &context) {
	if (_t->isEmpty()) {
		return;
	}

	_p = &p;
	_p->setFont(_t->_st->font);
	_palette = context.palette ? context.palette : &st::defaultTextPalette;
	_colors = context.colors;
	_originalPen = _p->pen();
	_originalPenSelected = (_palette->selectFg->c.alphaF() == 0)
		? _originalPen
		: _palette->selectFg->p;

	_x = _startLeft = context.position.x();
	_y = _startTop = context.position.y();
	_yFrom = context.clip.isNull() ? 0 : context.clip.y();
	_yTo = context.clip.isNull()
		? -1
		: (context.clip.y() + context.clip.height());
	const auto available = context.availableWidth
		? context.availableWidth
		: _t->maxWidth();
	_geometry = context.geometry.layout
		? context.geometry
		: SimpleGeometry(
			((context.useFullWidth || !(context.align & Qt::AlignLeft))
				? available
				: std::min(available, _t->maxWidth())),
			(context.elisionLines
				? context.elisionLines
				: (context.elisionHeight / _t->_st->font->height)),
			context.elisionRemoveFromEnd,
			context.elisionBreakEverywhere);
	_breakEverywhere = _geometry.breakEverywhere;
	_spoilerCache = context.spoiler;
	_selection = context.selection;
	_highlight = context.highlight;
	_fullWidthSelection = context.fullWidthSelection;
	_align = context.align;
	_cachedNow = context.now;
	_pausedEmoji = context.paused || context.pausedEmoji;
	_pausedSpoiler = context.paused || context.pausedSpoiler;
	_spoilerOpacity = _spoiler
		? (1. - _spoiler->revealAnimation.value(
			_spoiler->revealed ? 1. : 0.))
		: 0.;
	_quotePreCache = context.pre;
	_quoteBlockquoteCache = context.blockquote;
	_elisionMiddle = context.elisionMiddle && (context.elisionLines == 1);
	_linePostprocess = context.linePostprocess;
	enumerate();
}

void Renderer::enumerate() {
	Expects(!_geometry.outElided);

	_lineHeight = _t->lineHeight();
	_lineAscent = _t->_st->font->fascent;
	_blocksSize = _t->_blocks.size();
	_str = _t->_text.unicode();

	if (_p) {
		const auto clip = _p->hasClipping() ? _p->clipBoundingRect() : QRect();
		if (clip.width() > 0 || clip.height() > 0) {
			if (_yFrom < clip.y()) {
				_yFrom = clip.y();
			}
			if (_yTo < 0 || _yTo > clip.y() + clip.height()) {
				_yTo = clip.y() + clip.height();
			}
		}
	}

	if ((*_t->_blocks.cbegin())->type() != TextBlockType::Newline) {
		initNextParagraph(
			_t->_blocks.cbegin(),
			_t->_startQuoteIndex,
			UnpackParagraphDirection(
				_t->_startParagraphLTR,
				_t->_startParagraphRTL));
	}

	_fontHeight = _t->_st->font->height;
	auto last_rBearing = QFixed(0);
	_last_rPadding = QFixed(0);

	const auto guard = gsl::finally([&] {
		if (_p) {
			paintSpoilerRects();
		}
		if (_highlight) {
			composeHighlightPath();
		}
	});

	auto blockIndex = 0;
	auto longWordLine = true;
	auto lastWordStart = begin(_t->_words);
	auto lastWordStart_wLeft = _wLeft;
	auto e = end(_t->_words);
	for (auto w = begin(_t->_words); w != e; ++w) {
		if (w->newline()) {
			blockIndex = w->newlineBlockIndex();
			const auto qindex = _t->quoteIndex(_t->_blocks[blockIndex].get());
			const auto changed = (_quoteIndex != qindex);
			const auto hidden = !_quoteLinesLeft;
			if (_quoteLinesLeft) {
				--_quoteLinesLeft;
			}
			if (!hidden) {
				resolveLineGeometry(w->position());
				fillParagraphBg(changed ? _quotePadding.bottom() : 0);
				if (!drawLinePostprocessed(w->position(), begin(_t->_blocks) + blockIndex) && !_quoteExpandLinkLookup) {
					return;
				}
				_y += _lineHeight;
			}

			last_rBearing = 0;
			_last_rPadding = w->f_rpadding();

			initNextParagraph(
				begin(_t->_blocks) + blockIndex + 1,
				qindex,
				static_cast<const NewlineBlock*>(_t->_blocks[blockIndex].get())->paragraphDirection());

			_lineStartPadding = _last_rPadding;

			longWordLine = true;
			lastWordStart = w + 1;
			lastWordStart_wLeft = _wLeft;
			continue;
		} else if (!_quoteLinesLeft) {
			continue;
		}
		const auto wordEndsHere = !w->unfinished();

		auto w__f_width = w->f_width();
		const auto w__f_rbearing = w->f_rbearing();
		const auto newWidthLeft = _wLeft
			- last_rBearing
			- (_last_rPadding + w__f_width - w__f_rbearing);
		if (newWidthLeft >= 0
			|| (w->position() == _lineStart && !_elidedLine)) {
			last_rBearing = w__f_rbearing;
			_last_rPadding = w->f_rpadding();
			_wLeft = newWidthLeft;

			if (wordEndsHere) {
				longWordLine = false;
			}
			if (wordEndsHere || longWordLine) {
				lastWordStart = w + 1;
				lastWordStart_wLeft = _wLeft;
			}
			continue;
		}

		if (_elidedLine) {
		} else if (w != lastWordStart && !_breakEverywhere) {
			// word did not fit completely, so we roll back the state to the beginning of this long word
			w = lastWordStart;
			_wLeft = lastWordStart_wLeft;
			w__f_width = w->f_width();
		}
		const auto lineEnd = !_elidedLine
			? w->position()
			: (w + 1 != end(_t->_words))
			? (w + 1)->position()
			: int(_t->_text.size());
		if (_quoteLinesLeft) {
			--_quoteLinesLeft;
		}
		resolveLineGeometry(lineEnd);
		fillParagraphBg(0);
		while (_t->blockPosition(begin(_t->_blocks) + blockIndex + 1) < lineEnd) {
			++blockIndex;
		}
		if (!drawLinePostprocessed(lineEnd, begin(_t->_blocks) + blockIndex) && !_quoteExpandLinkLookup) {
			return;
		}
		_y += _lineHeight;
		_lineStart = w->position();
		_lineStartBlock = blockIndex;
		initNextLine();

		last_rBearing = w->f_rbearing();
		_last_rPadding = w->f_rpadding();
		_wLeft -= w__f_width - last_rBearing;

		longWordLine = !wordEndsHere;
		lastWordStart = w + 1;
		lastWordStart_wLeft = _wLeft;
	}
	if (_lineStart < _t->_text.size()) {
		if (_quoteLinesLeft) {
			--_quoteLinesLeft;

			resolveLineGeometry(_t->_text.size());
			fillParagraphBg(_quotePadding.bottom());
			if (!drawLinePostprocessed(_t->_text.size(), end(_t->_blocks))) {
				return;
			}
		}
	}
	if (!_p && _lookupSymbol) {
		_lookupResult.symbol = _t->_text.size();
		_lookupResult.afterSymbol = false;
	}
}

void Renderer::resolveLineGeometry(uint16 lineEnd) {
	const auto metrics = _t->resolveLineMetrics(
		_lineStart,
		lineEnd,
		_lineStartBlock);
	_lineHeight = metrics.height();
	_lineAscent = metrics.ascent;
	_yDelta = _lineAscent - _t->_st->font->fascent;
}

void Renderer::fillParagraphBg(int paddingBottom) {
	if (_quote) {
		const auto cutoff = _quote->collapsed
			&& ((!paddingBottom && !_quoteLinesLeft) // !expanded
				|| (paddingBottom // expanded
					&& _quoteLinesLeft + kQuoteCollapsedLines < -1));
		if (cutoff) {
			paddingBottom = _quotePadding.bottom();
		}
		const auto &st = _t->quoteStyle(_quote);
		const auto skip = st.verticalSkip;
		const auto isTop = (_y != _quoteLineTop);
		const auto isBottom = (paddingBottom != 0);
		const auto left = _startLeft + _quoteShift;
		const auto start = _quoteTop + skip;
		const auto top = _quoteLineTop + (isTop ? skip : 0);
		const auto fill = _y + _lineHeight + paddingBottom - top
			- (isBottom ? skip : 0);
		const auto rect = QRect(left, top, _startLineWidth, fill);

		const auto cache = (!_p || !_quote)
			? nullptr
			: _quote->pre
			? _quotePreCache
			: _quote->blockquote
			? _quoteBlockquoteCache
			: nullptr;
		if (cache) {
			auto &valid = _quote->pre
				? _quotePreValid
				: _quoteBlockquoteValid;
			if (!valid) {
				valid = true;
				ValidateQuotePaintCache(*cache, st);
			}
			FillQuotePaint(*_p, rect, *cache, st, {
				.skippedTop = uint32(top - start),
				.skipBottom = !isBottom,
				.expandIcon = cutoff && !_quote->expanded,
				.collapseIcon = cutoff && _quote->expanded,
			});
		}
		if (cutoff && _quoteExpandLinkLookup
			&& _lookupY >= start
			&& _lookupY < _quoteLineTop + _lineHeight + paddingBottom - skip
			&& _lookupX >= left
			&& _lookupX < left + _startLineWidth) {
			_quoteExpandLinkLookup = false;
			_quoteExpandLink = _quote->toggle;
		}
		if (isTop && st.header > 0) {
			if (_p) {
				const auto font = _t->_st->font->monospace();
				const auto topleft = rect.topLeft();
				const auto position = topleft + st.headerPosition;
				const auto lbaseline = position + QPoint(0, font->ascent);
				const auto headerText = _t->quoteHeaderText(_quote);
				_p->setFont(font);
				_p->setPen(_palette->monoFg->p);
				if (headerText.size() > kQuoteHeaderTextLarge) {
					const auto availableWidth = _startLineWidth
						- st.headerPosition.x()
						- st.iconPosition.x()
						- (!st.icon.empty() ? st.icon.width() : 0);
					if (font->width(headerText) > availableWidth) {
						_p->drawText(
							lbaseline,
							font->elided(headerText, availableWidth));
					} else {
						_p->drawText(lbaseline, headerText);
					}
				} else {
					_p->drawText(lbaseline, headerText);
				}
			} else if (_lookupX >= left
				&& _lookupX < left + _startLineWidth
				&& _lookupY >= top
				&& _lookupY < top + st.header) {
				if (_lookupLink) {
					_lookupResult.link = _quote->copy;
				}
				if (_lookupSymbol) {
					_lookupResult.symbol = _lineStart;
					_lookupResult.afterSymbol = false;
				}
			}
		}
	}
	_quoteLineTop = _y + _lineHeight + paddingBottom;
}

StateResult Renderer::getState(
		QPoint point,
		GeometryDescriptor geometry,
		StateRequest request) {
	if (_t->isEmpty() || point.y() < 0) {
		return {};
	}
	_lookupRequest = request;
	_lookupX = point.x();
	_lookupY = point.y();

	_lookupSymbol = (_lookupRequest.flags & StateRequest::Flag::LookupSymbol);
	_lookupLink = (_lookupRequest.flags & StateRequest::Flag::LookupLink);
	if (!_lookupSymbol && _lookupX < 0) {
		return {};
	}
	_geometry = std::move(geometry);
	_breakEverywhere = _geometry.breakEverywhere;
	_yFrom = _lookupY;
	_yTo = _lookupY + 1;
	_align = _lookupRequest.align;
	enumerate();
	if (_quoteExpandLink && !_lookupResult.link) {
		_lookupResult.link = _quoteExpandLink;
	}
	return _lookupResult;
}

crl::time Renderer::now() const {
	if (!_cachedNow) {
		_cachedNow = crl::now();
	}
	return _cachedNow;
}

void Renderer::initNextParagraph(
		Blocks::const_iterator i,
		int16 paragraphIndex,
		Qt::LayoutDirection direction) {
	_paragraphDirection = (direction == Qt::LayoutDirectionAuto)
		? style::LayoutDirection()
		: direction;
	_paragraphStartBlock = i;
	if (_quoteIndex != paragraphIndex) {
		_y += _quotePadding.bottom();
		_quoteIndex = paragraphIndex;
		_quote = _t->quoteByIndex(paragraphIndex);
		_quotePadding = _t->quotePadding(_quote);
		_quoteLinesLeft = _t->quoteLinesLimit(_quote);
		_quoteTop = _quoteLineTop = _y;
		_y += _quotePadding.top();
		_quotePadding.setTop(0);
		_quoteDirection = _paragraphDirection;
		_quoteExpandLinkLookup = _lookupLink
			&& _quote
			&& _quote->collapsed;
	}
	const auto e = _t->_blocks.cend();
	if (i == e) {
		_lineStart = _paragraphStart = _t->_text.size();
		_lineStartBlock = _t->_blocks.size();
		_paragraphLength = 0;
	} else {
		_lineStart = _paragraphStart = (*i)->position();
		_lineStartBlock = i - _t->_blocks.cbegin();
		for (; i != e; ++i) {
			if ((*i)->type() == TextBlockType::Newline) {
				break;
			}
		}
		_paragraphLength = ((i == e)
			? _t->_text.size()
			: (*i)->position())
			- _paragraphStart;
	}
	_paragraphAnalysis.resize(0);
	initNextLine();
}

void Renderer::initNextLine() {
	const auto line = _geometry.layout(_lineIndex++);
	_x = _startLeft + line.left + _quotePadding.left();
	_startLineWidth = line.width;
	_quoteShift = 0;
	if (_quote && _quote->maxWidth < _startLineWidth) {
		const auto delta = _startLineWidth - _quote->maxWidth;
		_startLineWidth = _quote->maxWidth;

		if (_align & Qt::AlignHCenter) {
			_quoteShift = delta / 2;
		} else if (((_align & Qt::AlignLeft)
			&& (_quoteDirection == Qt::RightToLeft))
			|| ((_align & Qt::AlignRight)
				&& (_quoteDirection == Qt::LeftToRight))) {
			_quoteShift = delta;
		}
		_x += _quoteShift;
	}
	_lineWidth = _startLineWidth
		- _quotePadding.left()
		- _quotePadding.right();
	_lineStartPadding = 0;
	_wLeft = _lineWidth;
	_elidedLine = line.elided;
}

void Renderer::initParagraphBidi() {
	if (!_paragraphLength || !_paragraphAnalysis.isEmpty()) {
		return;
	}

	_paragraphAnalysis.resize(_paragraphLength);
	BidiAlgorithm bidi(
		_str + _paragraphStart,
		_paragraphAnalysis.data(),
		_paragraphLength,
		(_paragraphDirection == Qt::RightToLeft),
		_paragraphStartBlock,
		_t->_blocks.cend(),
		_paragraphStart);
	bidi.process();
}

// The caret at an edge of the line - which edge is which depends on the
// direction the paragraph runs in.
void Renderer::lookupLineEdge(uint16 lineEnd, bool pastLineEnd) {
	const auto atLineEnd = (_paragraphDirection == Qt::RightToLeft)
		? !pastLineEnd
		: pastLineEnd;
	const auto hasSymbols = (lineEnd > _lineStart);
	_lookupResult.symbol = (atLineEnd && hasSymbols)
		? (lineEnd - 1)
		: _lineStart;
	_lookupResult.afterSymbol = (atLineEnd && hasSymbols);
}

bool Renderer::drawLinePostprocessed(
		uint16 lineEnd,
		Blocks::const_iterator blocksEnd) {
	if (!_linePostprocess || !_linePostprocess->method) {
		return drawLine(lineEnd, blocksEnd);
	}
	const auto index = _lineIndex - 1;
	auto postprocess = _linePostprocess->method(index);
	if (!postprocess) {
		return drawLine(lineEnd, blocksEnd);
	}

	const auto ratio = style::DevicePixelRatio();
	const auto lineAreaLeft = _x.toInt() - _quotePadding.left();
	const auto lineAreaWidth = _startLineWidth;
	const auto lineAreaTop = _y;
	const auto cacheWidth = lineAreaWidth * ratio;
	const auto cacheHeight = _lineHeight * ratio;

	auto &cache = *_linePostprocess->cache;
	if (cache.width() < cacheWidth || cache.height() < cacheHeight) {
		cache = QImage(
			std::max(cache.width(), cacheWidth),
			std::max(cache.height(), cacheHeight),
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(ratio);
	}
	cache.fill(Qt::transparent);

	auto savedP = _p;
	auto cachePainter = QPainter(&cache);
	cachePainter.setFont(savedP->font());
	cachePainter.setPen(savedP->pen());
	cachePainter.setRenderHints(savedP->renderHints());
	cachePainter.translate(-lineAreaLeft, -lineAreaTop);

	_p = &cachePainter;
	const auto result = drawLine(lineEnd, blocksEnd);
	cachePainter.end();

	_p = savedP;

	postprocess(cache);

	_p->drawImage(
		QRectF(lineAreaLeft, lineAreaTop, lineAreaWidth, _lineHeight),
		cache,
		QRectF(0, 0, cacheWidth, cacheHeight));

	return result;
}

bool Renderer::drawLine(uint16 lineEnd, Blocks::const_iterator blocksEnd) {
	if (_yTo >= 0 && _y >= _yTo) {
		return false;
	}
	if (_y + _lineHeight <= _yFrom) {
		if (_lookupSymbol) {
			_lookupResult.symbol = (lineEnd > _lineStart) ? (lineEnd - 1) : _lineStart;
			_lookupResult.afterSymbol = (lineEnd > _lineStart) ? true : false;
		}
		return !_elidedLine;
	}

	// Trimming pending spaces, because they sometimes don't fit on the line.
	// They also are not counted in the line width, they're in the right padding.
	// Line width is a sum of block / word widths and paddings between them, without trailing one.
	auto trimmedLineEnd = lineEnd;
	for (; trimmedLineEnd > _lineStart; --trimmedLineEnd) {
		auto ch = _t->_text[trimmedLineEnd - 1];
		if (ch != QChar::Space && ch != QChar::LineFeed) {
			break;
		}
	}

	auto endBlock = (blocksEnd == end(_t->_blocks)) ? nullptr : blocksEnd->get();
	if (_elidedLine) {
		// If we decided to draw the last line elided only because of the skip block
		// that did not fit on this line, we just draw the line till the very end.
		// Skip block is ignored in the elided lines, instead "removeFromEnd" is used.
		if (endBlock && endBlock->type() == TextBlockType::Skip) {
			endBlock = nullptr;
		}
		if (!endBlock) {
			_elidedLine = false;
		}
	}
	if (!_elidedLine && _elisionMiddle) {
		_elisionMiddle = false;
	}
	if (_elisionMiddle) {
		trimmedLineEnd = _t->blockEnd(end(_t->_blocks));
	}

	const auto startBlockIt = begin(_t->_blocks) + _lineStartBlock;
	const auto startBlock = startBlockIt->get();
	const auto startBlockEnd = _t->blockEnd(startBlockIt);
	if (!_elidedLine
		&& (trimmedLineEnd == _lineStart)
		&& (startBlock->type() == TextBlockType::CustomEmoji)
		&& (startBlockEnd > trimmedLineEnd)) {
		trimmedLineEnd = startBlockEnd;
	}

	const auto extendLeft = (startBlock->position() < _lineStart)
		? qMin(_lineStart - startBlock->position(), 2)
		: 0;
	_localFrom = _lineStart - extendLeft;
	const auto extendedLineEnd = (endBlock && endBlock->position() < trimmedLineEnd && !_elidedLine)
		? qMin(uint16(trimmedLineEnd + 2), _t->blockEnd(blocksEnd))
		: trimmedLineEnd;

	auto lineText = QString::fromRawData(
		_t->_text.constData() + _localFrom,
		extendedLineEnd - _localFrom);
	auto lineStart = extendLeft;
	auto lineLength = trimmedLineEnd - _lineStart;

	if (_elidedLine) {
		if (_elisionMiddle) {
			_paragraphLength = lineLength;
			initParagraphBidi();
		} else {
			initParagraphBidi();
			prepareElidedLine(lineText, lineStart, lineLength, endBlock);
		}
	}

	auto x = _x;
	if (_elisionMiddle) {
	} else if (_align & Qt::AlignHCenter) {
		x += (_wLeft / 2).toInt();
	} else if (((_align & Qt::AlignLeft)
		&& (_paragraphDirection == Qt::RightToLeft))
		|| ((_align & Qt::AlignRight)
			&& (_paragraphDirection == Qt::LeftToRight))) {
		x += _wLeft;
	}

	if (!_p) {
		if (_lookupX < x) {
			if (_lookupSymbol) {
				lookupLineEdge(lineEnd, false);
			}
			if (_lookupLink) {
				_lookupResult.link = nullptr;
			}
			_lookupResult.uponSymbol = false;
			return false;
		} else if (_lookupX >= x + (_lineWidth - _wLeft)) {
			lookupLineEdge(lineEnd, true);
			if (_lookupLink) {
				_lookupResult.link = nullptr;
			}
			_lookupResult.uponSymbol = false;
			return false;
		}
	}

	// Kept to the end of this call, where the items have been laid out and the
	// exact end of the text is known: taking what is left away from the line
	// width counts the last word's bearing, which the items do not, and the
	// difference left a gap. A line with no items has nothing to wait for.
	auto fillTillLineEnd = false;
	const auto fillSelectTillLineEnd = [&](QFixed from) {
		if (fillTillLineEnd) {
			fillTillLineEnd = false;
			fillSelectRange({ from, _x + _lineWidth });
		}
	};

	if (_fullWidthSelection) {
		const auto selectFromStart = (_selection.to > _lineStart)
			&& (_lineStart > 0)
			&& (_selection.from <= _lineStart);
		const auto selectTillEnd = (_selection.to > trimmedLineEnd)
			&& (trimmedLineEnd < _t->_text.size())
			&& (_selection.from <= trimmedLineEnd)
			&& (!endBlock || endBlock->type() != TextBlockType::Skip);

		if ((selectFromStart && _paragraphDirection == Qt::LeftToRight)
			|| (selectTillEnd && _paragraphDirection == Qt::RightToLeft)) {
			if (x > _x) {
				fillSelectRange({ _x, x });
			}
		}
		if ((selectTillEnd && _paragraphDirection == Qt::LeftToRight)
			|| (selectFromStart && _paragraphDirection == Qt::RightToLeft)) {
			if (x < _x + _wLeft) {
				fillTillLineEnd = true;
			}
		}
	}
	if (trimmedLineEnd == _lineStart && !_elidedLine) {
		fillSelectTillLineEnd(x);
		return true;
	}

	if (!_elidedLine) {
		initParagraphBidi(); // if was not inited
	}

	_f = _t->_st->font;
	auto leftLineLengthLeft = _elisionMiddle
		? (_lineWidth.toReal() - _f->elidew) / 2.
		: -1;
	auto rightLineLengthLeft = leftLineLengthLeft;
	auto engine = StackEngine(
		_t,
		_localFrom,
		lineText,
		gsl::span(_paragraphAnalysis).subspan(_localFrom - _paragraphStart),
		_lineStartBlock,
		_blocksSize);
	auto &e = engine.wrapped();

	int firstItem = e.findItem(lineStart), lastItem = e.findItem(lineStart + lineLength - 1);
	int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0;
	if (!nItems) {
		fillSelectTillLineEnd(x);
		return !_elidedLine;
	}

	int skipIndex = -1;
	QVarLengthArray<int> visualOrder(nItems);
	QVarLengthArray<uchar> levels(nItems);
	QVarLengthArray<std::vector<Block>::const_iterator> blocks(nItems);
	for (int i = 0; i < nItems; ++i) {
		const auto blockIt = blocks[i] = engine.shapeGetBlock(firstItem + i);
		auto &si = e.layoutData->items[firstItem + i];
		if ((*blockIt)->type() == TextBlockType::Skip) {
			levels[i] = si.analysis.bidiLevel = 0;
			skipIndex = i;
		} else {
			levels[i] = si.analysis.bidiLevel;
		}
	}
	// After the items are shaped: shaping grows the engine's memory block, and
	// the attributes live in it, so a pointer taken earlier can be left behind.
	// Only a click and a middle elision ask about letters at all.
	const auto attributes = (_lookupSymbol || _elisionMiddle)
		? e.attributes()
		: nullptr;

	QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());
	if (style::RightToLeft() && skipIndex == nItems - 1) {
		for (auto i = nItems; i > 1;) {
			--i;
			visualOrder[i] = visualOrder[i - 1];
		}
		visualOrder[0] = skipIndex;
	}

	auto textY = _y + _lineAscent;
	auto emojiY = (_t->_st->font->height - st::emojiSize) / 2;
	const auto customObjectRect = [&](
			CustomEmoji *custom,
			QFixed x,
			const std::optional<CustomEmojiVerticalMetrics> &vertical) {
		return QRect(
			x.toInt(),
			_y + (vertical
				? (_lineAscent - vertical->ascent)
				: (_yDelta + emojiY)).toInt(),
			custom->width(),
			vertical ? vertical->height() : st::emojiSize);
	};
	const auto fillBackground = [&](
			FixedRange range,
			int top,
			int height,
			const QBrush &brush) {
		if (range.empty() || brush.style() == Qt::NoBrush) {
			return;
		}
		const auto left = range.from.toInt();
		const auto width = range.till.toInt() - left;
		_p->fillRect(left, top, width, height, brush);
	};
	const auto fillMarked = [&](FixedRange range, int top, int height) {
		if (!_palette || !_palette->markBg->c.alpha()) {
			return;
		}
		fillBackground(range, top, height, _palette->markBg->b);
	};
	const auto fillColorizedBackground = [&](
			FixedRange range,
			FixedRange selected,
			int top,
			int height) {
		if (!_background.brush) {
			return;
		}
		if (selected.empty()) {
			fillBackground(range, top, height, *_background.brush);
			return;
		}
		if (range.from < selected.from) {
			fillBackground(
				{ range.from, selected.from },
				top,
				height,
				*_background.brush);
		}
		if (const auto selectedBrush = _background.brushSelected) {
			fillBackground(selected, top, height, *selectedBrush);
		} else if (_palette && _palette->selectBg->c.alpha()) {
			fillBackground(selected, top, height, _palette->selectBg->b);
		}
		if (selected.till < range.till) {
			fillBackground(
				{ selected.till, range.till },
				top,
				height,
				*_background.brush);
		}
	};

	auto lastLeftToMiddleX = x;

	_f = style::font();
	for (int i = 0; i < nItems; ++i) {
		const auto paintRightToMiddleElision = (leftLineLengthLeft == 0);
		const auto item = firstItem + visualOrder[paintRightToMiddleElision
			? (nItems - 1 - i)
			: i];
		const auto blockIt = blocks[item - firstItem];
		const auto block = blockIt->get();
		const auto isLastItem = (item == lastItem);
		const auto &si = e.layoutData->items.at(item);
		const auto rtl = (si.analysis.bidiLevel % 2);

		applyBlockProperties(e, block);
		const auto marked = (block->flags() & TextBlockFlag::Marked);
		const auto baselineShift = _t->blockBaselineShift(block);
		const auto textTop = (textY + baselineShift - _f->fascent).toInt();
		const auto textHeight = _f->height;
		if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
			const auto _type = block->type();
			if (!_p && _lookupX >= x && _lookupX < x + si.width) { // _lookupRequest
				if (_elisionMiddle) {
					return false;
				}
				if (_lookupLink) {
					if (_lookupY >= _y && _lookupY < _y + _lineHeight) {
						if (const auto link = lookupLink(block)) {
							_lookupResult.link = link;
						}
					}
				}
				if (_type != TextBlockType::Skip) {
					_lookupResult.uponSymbol = true;
				}
				if (_lookupSymbol) {
					if (_type == TextBlockType::Skip) {
						lookupLineEdge(trimmedLineEnd, true);
						return false;
					}

					// Emoji with spaces after symbol lookup
					auto chFrom = _str + _t->blockPosition(blockIt);
					auto chTo = _str + _t->blockEnd(blockIt);
					while (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space) {
						--chTo;
					}
					if (_lookupX < x + (block->objectWidth() / 2)) {
						_lookupResult.symbol = ((rtl && chTo > chFrom) ? (chTo - 1) : chFrom) - _str;
						_lookupResult.afterSymbol = (rtl && chTo > chFrom) ? true : false;
					} else {
						_lookupResult.symbol = ((rtl || chTo <= chFrom) ? chFrom : (chTo - 1)) - _str;
						_lookupResult.afterSymbol = (rtl || chTo <= chFrom) ? false : true;
					}
				}
				return false;
			} else if (_p
				&& (_type == TextBlockType::Emoji
					|| _type == TextBlockType::CustomEmoji)) {
				if (_elisionMiddle && !paintRightToMiddleElision) {
					if (leftLineLengthLeft - si.width.toReal() < 0) {
						leftLineLengthLeft = 0;
						i = -1;
						lastLeftToMiddleX = (x + si.width);
						_p->setPen(*_currentPen);
						rightLineLengthLeft = std::ceil((x).toReal()) - _x.toReal() - _f->elidew;
					} else {
						leftLineLengthLeft -= si.width.toReal();
						leftLineLengthLeft = std::max(0.01, leftLineLengthLeft);
					}
				} else if (_elisionMiddle && paintRightToMiddleElision && rightLineLengthLeft) {
					if (i == 0) {
						x = _x + _lineWidth;
					}
					if (rightLineLengthLeft - si.width.toReal() < 0) {
						rightLineLengthLeft = 0;
						i = nItems;
						{
							_p->setPen(*_currentPen);
							const auto bigWidth = x - lastLeftToMiddleX;
							const auto smallWidth = _f->elidew;
							const auto left = lastLeftToMiddleX;
							_p->setFont(WithFlags(
								_t->_st->font,
								(block->flags()
									& ~(TextBlockFlag::Subscript
										| TextBlockFlag::Superscript))));
							_p->drawText(
								(left + (bigWidth - smallWidth) / 2).toReal(),
								textY.toInt(),
								kQEllipsis);
						}
						continue;
					} else {
						rightLineLengthLeft -= si.width.toReal();
						rightLineLengthLeft = std::max(0.01, rightLineLengthLeft);
					}
					x -= si.width;
				}
				const auto fillSelect = _background.selectActiveBlock
					? FixedRange{ x, x + si.width }
					: findSelectObjectRange(
						si,
						blockIt,
						x,
						_selection);
				CustomEmoji *custom = nullptr;
				if (_type == TextBlockType::CustomEmoji) {
					custom = static_cast<const CustomEmojiBlock*>(block)->custom();
				}
				const auto vertical = custom
					? custom->vertical(*_t->_st)
					: std::nullopt;
				const auto box = custom
					? customObjectRect(
						custom,
						x,
						vertical)
					: QRect();
				if (custom) {
					if (marked) {
						fillMarked(
							{ x, x + si.width },
							box.top(),
							box.height());
					}
					if (_background.brush) {
						fillColorizedBackground(
							{ x, x + si.width },
							fillSelect,
							box.top(),
							box.height());
					} else {
						fillSelectRange(
							fillSelect,
							box.top(),
							box.height());
					}
				} else {
					if (_background.brush) {
						fillColorizedBackground(
							{ x, x + si.width },
							fillSelect,
							(_y + _yDelta).toInt(),
							_fontHeight);
					} else {
						fillSelectRange(fillSelect);
					}
				}
				if (_highlight) {
					pushHighlightRange(findSelectObjectRange(
						si,
						blockIt,
						x,
						_highlight->range));
				}

				const auto hasSpoiler = _background.spoiler
					&& (_spoilerOpacity > 0.);
				const auto fillSpoiler = hasSpoiler
					? FixedRange{ x, x + si.width }
					: FixedRange();
				const auto opacity = _p->opacity();
				if (!hasSpoiler || _spoilerOpacity < 1.) {
					if (hasSpoiler) {
						_p->setOpacity(opacity * (1. - _spoilerOpacity));
					}
					const auto ex = (x + st::emojiPadding).toInt();
					const auto ey = (_y + _yDelta + emojiY).toInt();
					if (_type == TextBlockType::Emoji) {
						Emoji::Draw(
							*_p,
							static_cast<const EmojiBlock*>(block)->emoji(),
							Emoji::GetSizeNormal(),
							ex,
							ey);
					} else if (_type == TextBlockType::CustomEmoji) {
						const auto custom = static_cast<const CustomEmojiBlock*>(block)->custom();
						const auto selected = (fillSelect.from <= x)
							&& (fillSelect.till > x);
						const auto color = (selected
							? _currentPenSelected
							: _currentPen)->color();
						if (!_customEmojiContext) {
							_customEmojiContext = CustomEmoji::Context{
								.textColor = color,
								.now = now(),
								.paused = _pausedEmoji,
							};
							_customEmojiSkip = (st::emojiSize
								- AdjustCustomEmojiSize(st::emojiSize)) / 2;
						} else {
							_customEmojiContext->textColor = color;
						}
						const auto replacementText = custom->replacementText();
						const auto semantics = custom->semantics();
						const auto showFallbackText = !semantics.isRealCustomEmoji
							&& !replacementText.isEmpty()
							&& !custom->ready()
							&& !custom->readyInDefaultState();
						if (!showFallbackText) {
							_customEmojiContext->position = vertical
								? box.topLeft()
								: QPoint(
									ex + _customEmojiSkip,
									ey + _customEmojiSkip);
							custom->paint(*_p, *_customEmojiContext);
						}
						if (showFallbackText) {
							_p->save();
							_p->setClipRect(box, Qt::IntersectClip);
							_p->setPen(((fillSelect.from <= x)
								&& (fillSelect.till > x))
								? *_currentPenSelected
								: *_currentPen);
							_p->drawText(
								box,
								Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
								replacementText);
							_p->restore();
						}
					}
					if (hasSpoiler) {
						_p->setOpacity(opacity);
					}
				}
				if (hasSpoiler) {
					// Elided item should be a text item
					// with '...' at the end, so this should not be it.
					const auto isElidedItem = false;
					pushSpoilerRange(
						fillSpoiler,
						fillSelect,
						isElidedItem,
						rtl);
				}
			//} else if (_p && currentBlock->type() == TextBlockSkip) { // debug
			//	_p->fillRect(QRect(x.toInt(), _y, currentBlock->width(), static_cast<SkipBlock*>(currentBlock)->height()), QColor(0, 0, 0, 32));
			}
			x += si.width;
			if (paintRightToMiddleElision) {
				x -= si.width;
			}
			continue;
		}

		unsigned short *logClusters = e.logClusters(&si);
		QGlyphLayout glyphs = e.shapedGlyphs(&si);

		int itemStart = qMax(lineStart, si.position), itemEnd;
		int itemLength = e.length(item);
		int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
		if (lineStart + lineLength < si.position + itemLength) {
			itemEnd = lineStart + lineLength;
			glyphsEnd = logClusters[itemEnd - si.position];
		} else {
			itemEnd = si.position + itemLength;
			glyphsEnd = si.num_glyphs;
		}

		const auto isSpaceGlyph = [&](int g) {
			// A cut can land past the last glyph of the item, where there is
			// no glyph to look at.
			if (g >= si.num_glyphs || !glyphs.attributes[g].dontPrint) {
				return false;
			}
			for (auto p = itemStart; p < itemEnd; ++p) {
				if (logClusters[p - si.position] == g) {
					return lineText.at(p).isSpace();
				}
			}
			return false;
		};

		QFixed itemWidth = 0;
		for (int g = glyphsStart; g < glyphsEnd; ++g)
			itemWidth += glyphs.effectiveAdvance(g);

		// Each half of a middle elision keeps the end of the item that faces
		// the middle of the line, and which end that is depends on the item's
		// direction: glyphs are stored in logical order, so the visually left
		// part of a right-to-left item is its last glyphs. Walking from the
		// first glyph regardless keeps the wrong end as soon as a half spans
		// more than one item - it would show neither the beginning nor the end
		// of the text, but two stretches out of its middle, out of order.
		// Glyph index each visible letter of the item starts at, so that a cut
		// lands between letters and not inside one - a glyph at a time used to
		// leave an orphan matra behind and let the line run past its width.
		auto letters = QVarLengthArray<int, 64>();

		// The characters those glyphs are for: a cut narrows both, or the
		// characters would keep describing the glyphs that were dropped.
		auto letterChars = QVarLengthArray<int, 64>();
		if (_elisionMiddle) {
			for (auto pos = itemStart; pos < itemEnd;) {
				letters.push_back(logClusters[pos - si.position]);
				letterChars.push_back(pos);
				pos = CutEnd(
					attributes,
					logClusters - si.position,
					pos,
					itemEnd);
			}
			letters.push_back(glyphsEnd);
			letterChars.push_back(itemEnd);
		}
		const auto lettersCount = int(letters.size()) - 1;
		if (_elisionMiddle && !paintRightToMiddleElision) {
			itemWidth = 0;
			const auto forward = !rtl;
			for (int k = 0; k < lettersCount; ++k) {
				const auto index = forward ? k : (lettersCount - 1 - k);
				const auto adv = GlyphsAdvance(
					glyphs,
					letters[index],
					letters[index + 1]);
				if (leftLineLengthLeft - adv.toReal() < 0) {
					leftLineLengthLeft = 0;
					const auto at = forward ? letters[index] : letters[index + 1];
					if (isSpaceGlyph(at)) {
						rightLineLengthLeft += _f->spacew;
					}
					if (forward) {
						glyphsEnd = letters[index];
						itemEnd = letterChars[index];
					} else {
						glyphsStart = at;
						itemStart = letterChars[index + 1];
					}
					i = -1;
					lastLeftToMiddleX = (x + itemWidth);
					break;
				} else {
					leftLineLengthLeft -= adv.toReal();
					leftLineLengthLeft = std::max(0.01, leftLineLengthLeft);
					itemWidth += adv;
				}
			}
		}
		if (_elisionMiddle && paintRightToMiddleElision && rightLineLengthLeft) {
			itemWidth = 0;
			if (i == 0) {
				x = _x + _lineWidth;
			}
			const auto forward = rtl;
			for (int k = 0; k < lettersCount; ++k) {
				const auto index = forward ? k : (lettersCount - 1 - k);
				const auto adv = GlyphsAdvance(
					glyphs,
					letters[index],
					letters[index + 1]);
				if (rightLineLengthLeft - adv.toReal() < 0) {
					rightLineLengthLeft = 0;
					const auto at = forward ? letters[index] : letters[index + 1];
					const auto space = isSpaceGlyph(at);
					if (forward) {
						glyphsEnd = at;
						itemEnd = letterChars[index];
					} else {
						glyphsStart = at;
						itemStart = letterChars[index + 1];
					}
					i = nItems;
					if (space) {
						x -= _f->spacew;
					}
					{
						_p->setPen(*_currentPen);
						const auto bigWidth = x - itemWidth - lastLeftToMiddleX;
						const auto smallWidth = _f->elidew;
						const auto left = lastLeftToMiddleX;
						_p->setFont(WithFlags(
							_t->_st->font,
							(block->flags()
								& ~(TextBlockFlag::Subscript
									| TextBlockFlag::Superscript))));
						_p->drawText(
							(left + (bigWidth - smallWidth) / 2).toReal(),
							textY.toInt(),
							kQEllipsis);
					}
					break;
				} else {
					rightLineLengthLeft -= adv.toReal();
					rightLineLengthLeft = std::max(0.01, rightLineLengthLeft);
					itemWidth += adv;
				}
			}
			x -= itemWidth;
		}

		if (!_p && _lookupX >= x && _lookupX < x + itemWidth) { // _lookupRequest
			if (_elisionMiddle) {
				return false;
			}
			if (_lookupLink) {
				if (_lookupY >= textTop && _lookupY < textTop + textHeight) {
					if (const auto link = lookupLink(block)) {
						_lookupResult.link = link;
					}
				}
			}
			_lookupResult.uponSymbol = true;
			if (_lookupSymbol) {
				QFixed tmpx = rtl ? (x + itemWidth) : x;
				const auto clusters = logClusters - si.position;
				auto letters = QVarLengthArray<int, 16>();
				for (int ch = 0, itemL = itemEnd - itemStart; ch < itemL;) {
					// A caret only goes before or after a whole visible letter,
					// and letters that share a glyph - a ligature, a consonant
					// with its matra - have no width of their own, so they take
					// an equal share of the one they are drawn as, which is how
					// the engine maps a point back to a position.
					const auto clusterEnd = ClusterEnd(
						clusters,
						itemStart + ch,
						itemEnd) - itemStart;
					const auto width = GlyphsAdvance(
						glyphs,
						clusters[itemStart + ch],
						(clusterEnd < itemL)
							? clusters[itemStart + clusterEnd]
							: glyphsEnd);
					letters.clear();
					for (auto i = ch; i != clusterEnd; ++i) {
						if (attributes[itemStart + i].graphemeBoundary) {
							letters.push_back(i);
						}
					}
					if (letters.isEmpty()) {
						letters.push_back(ch);
					}
					// Every boundary is measured from where the cluster
					// begins. Adding an equal share letter by letter would
					// drop what the division leaves over, and that remainder
					// would push everything after it: a line of ligatures
					// walks away from the text it is being compared with.
					const auto count = int(letters.size());
					const auto origin = tmpx;
					const auto at = [&](int part, int of) {
						const auto shift = width * part / of;
						return rtl ? (origin - shift) : (origin + shift);
					};
					for (auto k = 0; k != count; ++k) {
						const auto from = letters[k];
						const auto till = (k + 1 != count)
							? letters[k + 1]
							: clusterEnd;
						const auto edge = at(k + 1, count);
						const auto inside = rtl
							? (_lookupX >= edge)
							: (_lookupX < edge);
						if (inside) {
							const auto middle = at(2 * k + 1, 2 * count);
							const auto before = rtl
								? (_lookupX >= middle)
								: (_lookupX < middle);
							_lookupResult.symbol = _localFrom
								+ itemStart
								+ (before ? from : (till - 1));
							_lookupResult.afterSymbol = !before;
							return false;
						}
					}
					tmpx = rtl ? (origin - width) : (origin + width);
					ch = clusterEnd;
				}
				if (itemEnd > itemStart) {
					_lookupResult.symbol = _localFrom + itemEnd - 1;
					_lookupResult.afterSymbol = true;
				} else {
					_lookupResult.symbol = _localFrom + itemStart;
					_lookupResult.afterSymbol = false;
				}
			}
			return false;
		} else if (_p) {
			QTextItemInt gf;
			gf.glyphs = glyphs.mid(glyphsStart, glyphsEnd - glyphsStart);
			// A half of a middle elision can come out with no letter of its
			// own: the ellipsis is drawn either way, and half a letter is
			// worse than none.
			const auto drawGlyphs = [&](QPointF at, const QTextItemInt &item) {
				if (item.glyphs.numGlyphs > 0) {
					_p->drawTextItem(at, item);
				}
			};
			gf.f = &e.fnt;
			gf.chars = e.layoutData->string.unicode() + itemStart;
			gf.num_chars = itemEnd - itemStart;
			gf.fontEngine = e.fontEngine(si);
			gf.logClusters = logClusters + itemStart - si.position;
			gf.width = itemWidth;
			gf.justified = false;
			InitTextItemWithScriptItem(gf, si);
			if (!itemWidth) {
				gf.flags &= ~QTextItem::Underline;
				gf.underlineStyle = QTextCharFormat::NoUnderline;
			}

			const auto itemRange = FixedRange{ x, x + itemWidth };
			auto selectedRect = QRect();
			auto fillSelect = itemRange;
			if (!_background.selectActiveBlock) {
				fillSelect = findSelectTextRange(
					si,
					itemStart,
					itemEnd,
					x,
					itemWidth,
					gf,
					_selection);
				const auto from = fillSelect.from.toInt();
				selectedRect = QRect(
					from,
					textTop,
					fillSelect.till.toInt() - from,
					textHeight);
			}
			const auto hasSelected = !fillSelect.empty();
			const auto hasNotSelected = (fillSelect.from != itemRange.from)
				|| (fillSelect.till != itemRange.till);
			if (marked) {
				fillMarked(itemRange, textTop, textHeight);
			}
			if (_background.brush) {
				fillColorizedBackground(
					itemRange,
					fillSelect,
					textTop,
					textHeight);
			} else {
				fillSelectRange(fillSelect, textTop, textHeight);
			}
			if (i == nItems - 1) {
				// With the glyphs of the last item, not after them: the gap
				// this fills is where that item's last glyph reaches past its
				// advance, so a fill that came later would paint over the ink
				// it is there to sit behind.
				fillSelectTillLineEnd(x + itemWidth);
			}

			if (_highlight) {
				pushHighlightRange(findSelectTextRange(
					si,
					itemStart,
					itemEnd,
					x,
					itemWidth,
					gf,
					_highlight->range));
			}
			const auto hasSpoiler = _background.spoiler
				&& (_spoilerOpacity > 0.);
			const auto opacity = _p->opacity();
			const auto isElidedBlock = _indexOfElidedBlock
				== int(blockIt - begin(_t->_blocks));
			const auto isElidedItem = isElidedBlock && isLastItem;
			const auto complexClipping = hasSpoiler
				&& isElidedItem
				&& (_spoilerOpacity == 1.);
			if (!hasSpoiler || (_spoilerOpacity < 1.) || isElidedItem) {
				const auto complexClippingEnabled = complexClipping
					&& _p->hasClipping();
				const auto complexClippingRegion = complexClipping
					? _p->clipRegion()
					: QRegion();
				if (complexClipping) {
					const auto elided = isElidedBlock ? _f->elidew : 0;
					_p->setClipRect(
						QRect(
							(rtl
								? x.toInt()
								: (x + itemWidth).toInt() - elided),
							_y - _lineHeight,
							elided,
							_y + 2 * _lineHeight),
						Qt::IntersectClip);
				} else if (hasSpoiler && !isElidedItem) {
					_p->setOpacity(opacity * (1. - _spoilerOpacity));
				}
				const auto position = QPointF(
					x.toReal(),
					(textY + baselineShift).toReal());
				if (Q_UNLIKELY(hasSelected)) {
					if (Q_UNLIKELY(hasNotSelected)) {
						// The item is drawn twice, once clipped to the part
						// that is selected and once to what is left of it.
						// Both clips intersect into whatever is already set,
						// so the second one only has to name this item instead
						// of standing in for everything outside the selection,
						// and save/restore puts the clip back as it was rather
						// than rebuilding it from clipRegion().
						const auto outer = QRect(
							QPoint(
								x.toInt() - _lineHeight,
								_y - _lineHeight),
							QPoint(
								(x + itemWidth).toInt() + _lineHeight,
								_y + 2 * _lineHeight));

						_p->save();
						_p->setClipRect(selectedRect, Qt::IntersectClip);
						_p->setPen(*_currentPenSelected);
						drawGlyphs(position, gf);
						_p->restore();

						_p->save();
						_p->setClipRegion(
							QRegion(outer) - selectedRect,
							Qt::IntersectClip);
						_p->setPen(*_currentPen);
						drawGlyphs(position, gf);
						_p->restore();
					} else {
						_p->setPen(*_currentPenSelected);
						drawGlyphs(position, gf);
					}
				} else {
					_p->setPen(*_currentPen);
					drawGlyphs(position, gf);
				}
				if (complexClipping) {
					if (complexClippingEnabled) {
						_p->setClipRegion(complexClippingRegion);
					} else {
						_p->setClipping(false);
					}
				} else if (hasSpoiler && !isElidedItem) {
					_p->setOpacity(opacity);
				}
			}

			if (hasSpoiler) {
				pushSpoilerRange(
					itemRange,
					fillSelect,
					isElidedItem,
					rtl);
			}
		}

		x += itemWidth;
		if (paintRightToMiddleElision) {
			x -= itemWidth;
		}
	}
	if (!_p && !_elisionMiddle) {
		// Nothing on the line took the point, so it landed between where the
		// items ended and where the line is wide enough to be past: the line
		// width counts the last word's right bearing and the items do not.
		// A pixel there belongs at the end of the line, like a point past it,
		// and used to resolve to the very start of the text instead.
		lookupLineEdge(lineEnd, true);
	}
	fillSelectTillLineEnd(x);
	fillRectsFromRanges();
	return !_elidedLine;
}

FixedRange Renderer::findSelectObjectRange(
		const QScriptItem &si,
		std::vector<Block>::const_iterator blockIt,
		QFixed x,
		TextSelection selection) const {
	if (_localFrom + si.position >= selection.to) {
		return {};
	}
	auto chFrom = _str + _t->blockPosition(blockIt);
	auto chTo = _str + _t->blockEnd(blockIt);
	while (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space) {
		--chTo;
	}

	if (_localFrom + si.position >= selection.from) {
		return { x, x + si.width };
	}
	return {};
}

FixedRange Renderer::findSelectTextRange(
		const QScriptItem &si,
		int itemStart,
		int itemEnd,
		QFixed x,
		QFixed itemWidth,
		const QTextItemInt &gf,
		TextSelection selection) const {
	if (_localFrom + itemStart >= selection.to
		|| _localFrom + itemEnd <= selection.from) {
		return {};
	}
	auto selX = x;
	auto selWidth = itemWidth;
	const auto rtl = (si.analysis.bidiLevel % 2);
	if (_localFrom + itemStart < selection.from
		|| _localFrom + itemEnd > selection.to) {
		selWidth = 0;
		const auto itemL = itemEnd - itemStart;
		const auto selStart = std::max(
			selection.from - (_localFrom + itemStart),
			0);
		const auto selEnd = std::min(
			selection.to - (_localFrom + itemStart),
			itemL);
		const auto lczero = gf.logClusters[0];
		for (int ch = 0, g; ch < selEnd;) {
			g = gf.logClusters[ch];
			// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
			int ch2 = ch + 1;
			while ((ch2 < itemL) && (g == gf.logClusters[ch2])) {
				++ch2;
			}
			// The whole cluster, not just its first glyph: otherwise selX
			// falls behind the text as it is drawn and the highlight starts
			// left of the characters it covers.
			const auto gwidth = GlyphsAdvance(
				gf.glyphs,
				g - lczero,
				((ch2 < itemL) ? gf.logClusters[ch2] : (lczero + gf.glyphs.numGlyphs))
					- lczero);
			if (ch2 <= selStart) {
				selX += gwidth;
			} else if (ch >= selStart && ch2 <= selEnd) {
				selWidth += gwidth;
			} else {
				int sStart = ch, sEnd = ch2;
				if (ch < selStart) {
					sStart = selStart;
					selX += QFixed(sStart - ch) * gwidth / QFixed(ch2 - ch);
				}
				if (ch2 >= selEnd) {
					sEnd = selEnd;
					selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
					break;
				}
				selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
			}
			ch = ch2;
		}
	}
	if (rtl) selX = x + itemWidth - (selX - x) - selWidth;

	return { selX, selX + selWidth };
}

void Renderer::fillSelectRange(FixedRange range) {
	fillSelectRange(range, (_y + _yDelta).toInt(), _fontHeight);
}

void Renderer::fillSelectRange(FixedRange range, int top, int height) {
	if (range.empty()) {
		return;
	}

	const auto defaultLineTop = _y + _lineAscent - _t->_st->font->fascent;
	const auto defaultLineBottom = defaultLineTop + _t->_st->font->height;
	const auto bottom = std::max(top + height, defaultLineBottom.toInt());

	top = std::min(top, defaultLineTop.toInt());
	height = bottom - top;

	const auto left = range.from.toInt();
	const auto width = range.till.toInt() - left;
	_p->fillRect(left, top, width, height, _palette->selectBg);
}

void Renderer::pushHighlightRange(FixedRange range) {
	if (range.empty()) {
		return;
	}
	AppendRange(_highlightRanges, range);
}

void Renderer::pushSpoilerRange(
		FixedRange range,
		FixedRange selected,
		bool isElidedItem,
		bool rtl) {
	if (!_background.spoiler || !_spoiler) {
		return;
	} else if (isElidedItem) {
		const auto elided = _f->elidew;
		if (rtl) {
			range.from += elided;
		} else {
			range.till -= elided;
		}
	}
	if (range.empty()) {
		return;
	} else if (selected.empty() || !Intersects(range, selected)) {
		AppendRange(_spoilerRanges, range);
	} else {
		AppendRange(_spoilerRanges, { range.from, selected.from });
		AppendRange(_spoilerSelectedRanges, Intersected(range, selected));
		AppendRange(_spoilerRanges, { selected.till, range.till });
	}
}

void Renderer::fillRectsFromRanges() {
	fillRectsFromRanges(_spoilerRects, _spoilerRanges);
	fillRectsFromRanges(_spoilerSelectedRects, _spoilerSelectedRanges);
	fillRectsFromRanges(_highlightRects, _highlightRanges);
}

void Renderer::fillRectsFromRanges(
		QVarLengthArray<QRect, kSpoilersRectsSize> &rects,
		QVarLengthArray<FixedRange> &ranges) {
	if (ranges.empty()) {
		return;
	}
	auto lastTill = ranges.front().from.toInt() - 1;
	const auto y = (_y + _yDelta).toInt();
	const auto height = _fontHeight;
	for (const auto &range : ranges) {
		auto from = range.from.toInt();
		auto till = range.till.toInt();
		if (from <= lastTill) {
			auto &last = rects.back();
			from = std::min(from, last.x());
			till = std::max(till, last.x() + last.width());
			last = { from, y, till - from, height };
		} else {
			rects.push_back({ from, y, till - from, height });
		}
		lastTill = till;
	}
	ranges.clear();
}

void Renderer::paintSpoilerRects() {
	Expects(_p != nullptr);

	if (!_spoiler) {
		return;
	}
	const auto opacity = _p->opacity();
	if (_spoilerOpacity < 1.) {
		_p->setOpacity(opacity * _spoilerOpacity);
	}
	const auto index = _spoiler->animation.index(now(), _pausedSpoiler);
	paintSpoilerRects(
		_spoilerRects,
		_palette->spoilerFg,
		index);
	paintSpoilerRects(
		_spoilerSelectedRects,
		_palette->selectSpoilerFg,
		index);
	if (_spoilerOpacity < 1.) {
		_p->setOpacity(opacity);
	}
}

void Renderer::paintSpoilerRects(
		const QVarLengthArray<QRect, kSpoilersRectsSize> &rects,
		const style::color &color,
		int index) {
	if (rects.empty()) {
		return;
	}
	const auto frame = _spoilerCache->lookup(color->c)->frame(index);
	if (_spoilerCache) {
		for (const auto &rect : rects) {
			Ui::FillSpoilerRect(*_p, rect, frame, -rect.topLeft());
		}
	} else {
		// Show forgotten spoiler context part.
		for (const auto &rect : rects) {
			_p->fillRect(rect, Qt::red);
		}
	}
}

void Renderer::composeHighlightPath() {
	Expects(_highlight != nullptr);
	Expects(_highlight->outPath != nullptr);

	if (_highlight->interpolateProgress >= 1.) {
		_highlight->outPath->addRect(_highlight->interpolateTo);
	} else if (_highlight->interpolateProgress <= 0.) {
		for (const auto &rect : _highlightRects) {
			_highlight->outPath->addRect(rect);
		}
	} else {
		const auto to = _highlight->interpolateTo;
		const auto progress = _highlight->interpolateProgress;
		const auto lerp = [=](int from, int to) {
			return from + (to - from) * progress;
		};
		for (const auto &rect : _highlightRects) {
			_highlight->outPath->addRect(
				lerp(rect.x(), to.x()),
				lerp(rect.y(), to.y()),
				lerp(rect.width(), to.width()),
				lerp(rect.height(), to.height()));
		}
	}
}

const AbstractBlock *Renderer::markBlockForElisionGetEnd(int blockIndex) {
	if (_elideSavedBlock) {
		restoreAfterElided();
	}
	if (_t->_blocks[blockIndex]->type() != TextBlockType::Text) {
		_elideSavedIndex = blockIndex;
		auto mutableText = const_cast<String*>(_t);
		_elideSavedBlock = std::move(mutableText->_blocks[blockIndex]);
		mutableText->_blocks[blockIndex] = Block::Text({
			.position = (*_elideSavedBlock)->position(),
			.flags = (*_elideSavedBlock)->flags(),
			.linkIndex = (*_elideSavedBlock)->linkIndex(),
			.colorIndex = (*_elideSavedBlock)->colorIndex(),
			.bgIndex = (*_elideSavedBlock)->bgIndex(),
		});
	}
	_indexOfElidedBlock = blockIndex;
	_blocksSize = blockIndex + 1;
	return (blockIndex + 1 < _t->_blocks.size())
		? _t->_blocks[blockIndex + 1].get()
		: nullptr;
}

void Renderer::setElideBidi(int elideStart) {
	const auto elideLength = kQEllipsis.size();
	const auto newParLength = elideStart + elideLength - _paragraphStart;
	if (newParLength > _paragraphAnalysis.size()) {
		_paragraphAnalysis.resize(newParLength);
	}
	const auto bidiLevel = (newParLength > elideLength)
		? _paragraphAnalysis[newParLength - elideLength - 1].bidiLevel
		: (_paragraphDirection == Qt::RightToLeft)
		? 1
		: 0;
	for (auto i = elideLength; i > 0; --i) {
		_paragraphAnalysis[newParLength - i].bidiLevel = bidiLevel;
	}
}

void Renderer::prepareElidedLine(
		QString &lineText,
		int lineStart,
		int &lineLength,
		const AbstractBlock *&endBlock,
		int recursed) {
	_f = _t->_st->font;
	auto engine = StackEngine(
		_t,
		_localFrom,
		lineText,
		gsl::span(_paragraphAnalysis).subspan(_localFrom - _paragraphStart),
		_lineStartBlock,
		_blocksSize);
	auto &e = engine.wrapped();

	// Refreshed after every shape below: shaping grows the engine's memory
	// block, and the attributes live in it.
	auto attributes = (const QCharAttributes*)nullptr;
	_wLeft = _lineWidth
		- _lineStartPadding
		- _quotePadding.left()
		- _quotePadding.right();

	const auto firstItem = e.findItem(lineStart);
	const auto lastItem = e.findItem(lineStart + lineLength - 1);
	const auto nItems = (firstItem >= 0 && lastItem >= firstItem)
		? (lastItem - firstItem + 1)
		: 0;
	auto elisionWidth = _t->_st->font->elidew;
	for (auto i = 0; i < nItems; ++i) {
		const auto blockIt = engine.shapeGetBlock(firstItem + i);
		attributes = e.attributes();
		const auto block = blockIt->get();
		const auto blockIndex = int(blockIt - begin(_t->_blocks));
		const auto nextBlock = (blockIndex + 1 < _blocksSize)
			? _t->_blocks[blockIndex + 1].get()
			: nullptr;
		const auto font = WithFlags(_t->_st->font, block->flags());
		elisionWidth = font->elidew;
		auto &si = e.layoutData->items[firstItem + i];
		const auto _type = block->type();
		if (_type == TextBlockType::Emoji
			|| _type == TextBlockType::CustomEmoji
			|| _type == TextBlockType::Skip
			|| _type == TextBlockType::Newline) {
			if (_wLeft < elisionWidth + si.width) {
				_wLeft -= elisionWidth;
				prepareElisionAt(lineText, lineLength, block->position());
				endBlock = markBlockForElisionGetEnd(blockIndex);
				return;
			}
			_wLeft -= si.width;
		} else if (_type == TextBlockType::Text) {
			unsigned short *logClusters = e.logClusters(&si);
			QGlyphLayout glyphs = e.shapedGlyphs(&si);

			int itemStart = qMax(lineStart, si.position), itemEnd;
			int itemLength = e.length(firstItem + i);
			int glyphsEnd;
			if (lineStart + lineLength < si.position + itemLength) {
				itemEnd = lineStart + lineLength;
				glyphsEnd = logClusters[itemEnd - si.position];
			} else {
				itemEnd = si.position + itemLength;
				glyphsEnd = si.num_glyphs;
			}

			// A whole visible letter at a time, so the ellipsis never replaces
			// half of one - see the elision in drawLine() for the same reason.
			for (auto pos = itemStart; pos < itemEnd;) {
				const auto till = CutEnd(
					attributes,
					logClusters - si.position,
					pos,
					itemEnd);
				const auto adv = GlyphsAdvance(
					glyphs,
					logClusters[pos - si.position],
					(till < itemEnd)
						? logClusters[till - si.position]
						: glyphsEnd);
				if (_wLeft < elisionWidth + adv) {
					_wLeft -= elisionWidth;

					if (lineText.size() <= pos || recursed > 3) {
						prepareElisionAt(lineText, lineLength, _localFrom + pos);
						endBlock = markBlockForElisionGetEnd(blockIndex);
						return;
					}
					lineText = lineText.mid(0, pos);
					lineLength = _localFrom + pos - _lineStart;
					_blocksSize = blockIndex + 1;
					endBlock = nextBlock;
					prepareElidedLine(lineText, lineStart, lineLength, endBlock, recursed + 1);
					return;
				} else {
					_wLeft -= adv;
				}
				pos = till;
			}
		}
	}

	_wLeft -= elisionWidth;

	const auto elideStart = _localFrom + lineText.size();
	auto blockIndex = engine.blockIndex(lineText.size() - 1);
	for (; blockIndex + 1 < _blocksSize && _t->_blocks[blockIndex]->position() < elideStart; ++blockIndex) {
	}
	prepareElisionAt(lineText, lineLength, elideStart);
	if (recursed) {
		_indexOfElidedBlock = blockIndex;
	} else {
		endBlock = markBlockForElisionGetEnd(blockIndex);
	}
}

void Renderer::prepareElisionAt(
		QString &lineText,
		int &lineLength,
		uint16 position) {
	lineText = lineText.mid(0, position - _localFrom) + kQEllipsis;
	lineLength = position + kQEllipsis.size() - _lineStart;
	_selection.to = qMin(_selection.to, position);
	setElideBidi(position);
}

void Renderer::restoreAfterElided() {
	if (_elideSavedBlock) {
		const_cast<String*>(_t)->_blocks[_elideSavedIndex] = std::move(*_elideSavedBlock);
	}
}

void Renderer::applyBlockProperties(
		QTextEngine &e,
		not_null<const AbstractBlock*> block) {
	const auto flags = block->flags();
	const auto usedFont = [&] {
		if (const auto index = block->linkIndex()) {
			const auto underline = _t->_st->linkUnderline;
			const auto underlined = (underline == st::kLinkUnderlineNever)
				? false
				: (underline == st::kLinkUnderlineActive)
				? ((_palette && _palette->linkAlwaysActive)
					|| ClickHandler::showAsActive(_t->_extended
						? _t->_extended->links[index - 1]
						: nullptr))
				: true;
			return underlined ? _t->_st->font->underline() : _t->_st->font;
		}
		return _t->_st->font;
	}();
	const auto newFont = WithFlags(usedFont, block->flags());
	if (_f != newFont) {
		_f = newFont;
		const auto use = (_f->family() == _t->_st->font->family())
			? WithFlags(_t->_st->font, flags, _f->flags())
			: _f;
		e.fnt = use->f;
		e.resetFontEngineCache();
	}
	if (_p) {
		const auto flags = block->flags();
		const auto isMono = IsMono(flags);
		_background = {};
		if ((flags & TextBlockFlag::Spoiler) && _spoiler) {
			_background.spoiler = true;
		}
		if (isMono
			&& block->linkIndex()
			&& (!_background.spoiler || _spoiler->revealed)) {
			const auto pressed = ClickHandler::showAsPressed(_t->_extended
				? _t->_extended->links[block->linkIndex() - 1]
				: nullptr);
			_background.selectActiveBlock = pressed;
		}

		if (const auto color = block->colorIndex()) {
			if (color == 1) {
				if (_quote && _quote->blockquote && _quoteBlockquoteCache) {
					_quoteLinkPenOverride = QPen(_quoteBlockquoteCache->icon);
					_currentPen = &_quoteLinkPenOverride;
					_currentPenSelected = &_quoteLinkPenOverride;
				} else {
					_currentPen = &_palette->linkFg->p;
					_currentPenSelected = &_palette->selectLinkFg->p;
				}
			} else if (color - 1 <= _colors.size()) {
				_currentPen = _colors[color - 2].pen;
				_currentPenSelected = _colors[color - 2].penSelected;
			} else {
				_currentPen = &_originalPen;
				_currentPenSelected = &_originalPenSelected;
			}
			if (const auto bgIndex = block->bgIndex()
				; bgIndex && (bgIndex <= _colors.size())) {
				_background.brush = _colors[bgIndex - 1].bg;
				_background.brushSelected = _colors[bgIndex - 1].bgSelected;
			}
		} else if (isMono) {
			_currentPen = &_palette->monoFg->p;
			_currentPenSelected = &_palette->selectMonoFg->p;
		} else if (block->linkIndex()) {
			if (_quote && _quote->blockquote && _quoteBlockquoteCache) {
				_quoteLinkPenOverride = QPen(_quoteBlockquoteCache->icon);
				_currentPen = &_quoteLinkPenOverride;
				_currentPenSelected = &_quoteLinkPenOverride;
			} else {
				_currentPen = &_palette->linkFg->p;
				_currentPenSelected = &_palette->selectLinkFg->p;
			}
		} else {
			_currentPen = &_originalPen;
			_currentPenSelected = &_originalPenSelected;
		}
	}
}

ClickHandlerPtr Renderer::lookupLink(const AbstractBlock *block) const {
	const auto spoilerLink = (_spoiler
		&& !_spoiler->revealed
		&& (block->flags() & TextBlockFlag::Spoiler))
		? _spoiler->link
		: ClickHandlerPtr();
	if (spoilerLink) {
		return spoilerLink;
	}
	if (!block->linkIndex()) {
		if (block->type() != TextBlockType::CustomEmoji
			|| !_t->_extended) {
			return nullptr;
		}
		const auto customEmoji = _t->_extended->customEmoji;
		if (!customEmoji) {
			return nullptr;
		}
		const auto customBlock = static_cast<const CustomEmojiBlock*>(block);
		const auto custom = customBlock->custom();
		if (!custom->semantics().allowCustomEmojiClick) {
			return nullptr;
		}
		const auto entityData = custom->entityData();
		if (customEmoji->predicate
			&& !customEmoji->predicate(entityData)) {
			return nullptr;
		}
		const auto same = customEmoji->link
			&& (customEmoji->entityData == entityData);
		if (customEmoji->link
			&& ClickHandler::getPressed() == customEmoji->link) {
			customEmoji->pressedLink = customEmoji->link;
			return same ? customEmoji->link : CustomEmojiMismatchLink();
		}
		if (!same) {
			customEmoji->entityData = entityData;
			customEmoji->link = std::make_shared<CustomEmojiClickHandler>(
				customEmoji,
				entityData);
		}
		return customEmoji->link;
	}
	return _t->_extended
		? _t->_extended->links[block->linkIndex() - 1]
		: nullptr;
}

} // namespace Ui::Text
