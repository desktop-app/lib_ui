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
#include "styles/style_basic.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <private/qharfbuzz_p.h>
#endif // Qt < 6.0.0

namespace Ui::Text {
namespace {

constexpr auto kMaxItemLength = 4096;

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
	_geometry = context.geometry.layout
		? context.geometry
		: SimpleGeometry(
			((context.useFullWidth || !(context.align & Qt::AlignLeft))
				? context.availableWidth
				: std::min(context.availableWidth, _t->maxWidth())),
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
	enumerate();
}

void Renderer::enumerate() {
	Expects(!_geometry.outElided);

	_lineHeight = _t->lineHeight();
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

	_lineHeight = _t->lineHeight();
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
				fillParagraphBg(changed ? _quotePadding.bottom() : 0);
				if (!drawLine(w->position(), begin(_t->_blocks) + blockIndex) && !_quoteExpandLinkLookup) {
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
		fillParagraphBg(0);
		while (_t->blockPosition(begin(_t->_blocks) + blockIndex + 1) < lineEnd) {
			++blockIndex;
		}
		if (!drawLine(lineEnd, begin(_t->_blocks) + blockIndex) && !_quoteExpandLinkLookup) {
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

			fillParagraphBg(_quotePadding.bottom());
			if (!drawLine(_t->_text.size(), end(_t->_blocks))) {
				return;
			}
		}
	}
	if (!_p && _lookupSymbol) {
		_lookupResult.symbol = _t->_text.size();
		_lookupResult.afterSymbol = false;
	}
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
				_p->setFont(font);
				_p->setPen(_palette->monoFg->p);
				_p->drawText(lbaseline, _t->quoteHeaderText(_quote));
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

bool Renderer::drawLine(uint16 lineEnd, Blocks::const_iterator blocksEnd) {
	_yDelta = (_lineHeight - _fontHeight) / 2;
	if (_yTo >= 0 && (_y + _yDelta >= _yTo || _y >= _yTo)) {
		return false;
	}
	if (_y + _yDelta + _fontHeight <= _yFrom) {
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

	const auto startBlock = _t->_blocks[_lineStartBlock].get();

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
		initParagraphBidi();
		prepareElidedLine(lineText, lineStart, lineLength, endBlock);
	}

	auto x = _x;
	if (_align & Qt::AlignHCenter) {
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
				if (_paragraphDirection == Qt::RightToLeft) {
					_lookupResult.symbol = (lineEnd > _lineStart) ? (lineEnd - 1) : _lineStart;
					_lookupResult.afterSymbol = (lineEnd > _lineStart) ? true : false;
					//_lookupResult.uponSymbol = ((_lookupX >= _x) && (lineEnd < _t->_text.size()) && (!endBlock || endBlock->type() != TextBlockType::Skip)) ? true : false;
				} else {
					_lookupResult.symbol = _lineStart;
					_lookupResult.afterSymbol = false;
					//_lookupResult.uponSymbol = ((_lookupX >= _x) && (_lineStart > 0)) ? true : false;
				}
			}
			if (_lookupLink) {
				_lookupResult.link = nullptr;
			}
			_lookupResult.uponSymbol = false;
			return false;
		} else if (_lookupX >= x + (_lineWidth - _wLeft)) {
			if (_paragraphDirection == Qt::RightToLeft) {
				_lookupResult.symbol = _lineStart;
				_lookupResult.afterSymbol = false;
				//_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (_lineStart > 0)) ? true : false;
			} else {
				_lookupResult.symbol = (lineEnd > _lineStart) ? (lineEnd - 1) : _lineStart;
				_lookupResult.afterSymbol = (lineEnd > _lineStart) ? true : false;
				//_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (lineEnd < _t->_text.size()) && (!endBlock || endBlock->type() != TextBlockType::Skip)) ? true : false;
			}
			if (_lookupLink) {
				_lookupResult.link = nullptr;
			}
			_lookupResult.uponSymbol = false;
			return false;
		}
	}

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
				fillSelectRange({ x + _lineWidth - _wLeft, _x + _lineWidth });
			}
		}
	}
	if (trimmedLineEnd == _lineStart && !_elidedLine) {
		return true;
	}

	if (!_elidedLine) {
		initParagraphBidi(); // if was not inited
	}

	_f = _t->_st->font;
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
	QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());
	if (style::RightToLeft() && skipIndex == nItems - 1) {
		for (auto i = nItems; i > 1;) {
			--i;
			visualOrder[i] = visualOrder[i - 1];
		}
		visualOrder[0] = skipIndex;
	}

	auto textY = _y + _yDelta + _t->_st->font->ascent;
	auto emojiY = (_t->_st->font->height - st::emojiSize) / 2;

	_f = style::font();
	for (int i = 0; i < nItems; ++i) {
		const auto item = firstItem + visualOrder[i];
		const auto blockIt = blocks[item - firstItem];
		const auto block = blockIt->get();
		const auto isLastItem = (item == lastItem);
		const auto &si = e.layoutData->items.at(item);
		const auto rtl = (si.analysis.bidiLevel % 2);

		applyBlockProperties(e, block);
		if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
			const auto _type = block->type();
			if (!_p && _lookupX >= x && _lookupX < x + si.width) { // _lookupRequest
				if (_lookupLink) {
					if (_lookupY >= _y + _yDelta && _lookupY < _y + _yDelta + _fontHeight) {
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
						if (_paragraphDirection == Qt::RightToLeft) {
							_lookupResult.symbol = _lineStart;
							_lookupResult.afterSymbol = false;
						} else {
							_lookupResult.symbol = (trimmedLineEnd > _lineStart) ? (trimmedLineEnd - 1) : _lineStart;
							_lookupResult.afterSymbol = (trimmedLineEnd > _lineStart) ? true : false;
						}
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
				const auto fillSelect = _background.selectActiveBlock
					? FixedRange{ x, x + si.width }
					: findSelectEmojiRange(
						si,
						blockIt,
						x,
						_selection);
				fillSelectRange(fillSelect);
				if (_highlight) {
					pushHighlightRange(findSelectEmojiRange(
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
					const auto ey = _y + _yDelta + emojiY;
					if (_type == TextBlockType::Emoji) {
						Emoji::Draw(
							*_p,
							static_cast<const EmojiBlock*>(block)->emoji(),
							Emoji::GetSizeNormal(),
							ex,
							ey);
					} else {
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
						_customEmojiContext->position = {
							ex + _customEmojiSkip,
							ey + _customEmojiSkip,
						};
						custom->paint(*_p, *_customEmojiContext);
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

		QFixed itemWidth = 0;
		for (int g = glyphsStart; g < glyphsEnd; ++g)
			itemWidth += glyphs.effectiveAdvance(g);

		if (!_p && _lookupX >= x && _lookupX < x + itemWidth) { // _lookupRequest
			if (_lookupLink) {
				if (_lookupY >= _y + _yDelta && _lookupY < _y + _yDelta + _fontHeight) {
					if (const auto link = lookupLink(block)) {
						_lookupResult.link = link;
					}
				}
			}
			_lookupResult.uponSymbol = true;
			if (_lookupSymbol) {
				QFixed tmpx = rtl ? (x + itemWidth) : x;
				for (int ch = 0, g, itemL = itemEnd - itemStart; ch < itemL;) {
					g = logClusters[itemStart - si.position + ch];
					QFixed gwidth = glyphs.effectiveAdvance(g);
					// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
					int ch2 = ch + 1;
					while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
						++ch2;
					}
					for (int charsCount = (ch2 - ch); ch < ch2; ++ch) {
						QFixed shift1 = QFixed(2 * (charsCount - (ch2 - ch)) + 2) * gwidth / QFixed(2 * charsCount),
							shift2 = QFixed(2 * (charsCount - (ch2 - ch)) + 1) * gwidth / QFixed(2 * charsCount);
						if ((rtl && _lookupX >= tmpx - shift1) ||
							(!rtl && _lookupX < tmpx + shift1)) {
							_lookupResult.symbol = _localFrom + itemStart + ch;
							if ((rtl && _lookupX >= tmpx - shift2) ||
								(!rtl && _lookupX < tmpx + shift2)) {
								_lookupResult.afterSymbol = false;
							} else {
								_lookupResult.afterSymbol = true;
							}
							return false;
						}
					}
					if (rtl) {
						tmpx -= gwidth;
					} else {
						tmpx += gwidth;
					}
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
			gf.f = &e.fnt;
			gf.chars = e.layoutData->string.unicode() + itemStart;
			gf.num_chars = itemEnd - itemStart;
			gf.fontEngine = e.fontEngine(si);
			gf.logClusters = logClusters + itemStart - si.position;
			gf.width = itemWidth;
			gf.justified = false;
			InitTextItemWithScriptItem(gf, si);

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
					_y + _yDelta,
					fillSelect.till.toInt() - from,
					_fontHeight);
			}
			const auto hasSelected = !fillSelect.empty();
			const auto hasNotSelected = (fillSelect.from != itemRange.from)
				|| (fillSelect.till != itemRange.till);
			fillSelectRange(fillSelect);

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
				if (Q_UNLIKELY(hasSelected)) {
					if (Q_UNLIKELY(hasNotSelected)) {
						// There is a bug in retina QPainter clipping stack.
						// You can see glitches in rendering in such text:
						// aA
						// Aa
						// Where selection is both 'A'-s.
						// I can't debug it right now, this is a workaround.
#ifdef Q_OS_MAC
						_p->save();
#endif // Q_OS_MAC
						const auto clippingEnabled = _p->hasClipping();
						const auto clippingRegion = _p->clipRegion();
						_p->setClipRect(selectedRect, Qt::IntersectClip);
						_p->setPen(*_currentPenSelected);
						_p->drawTextItem(QPointF(x.toReal(), textY), gf);
						const auto externalClipping = clippingEnabled
							? clippingRegion
							: QRegion(QRect(
								(_x - _lineWidth).toInt(),
								_y - _lineHeight,
								(_x + 2 * _lineWidth).toInt(),
								_y + 2 * _lineHeight));
						_p->setClipRegion(externalClipping - selectedRect);
						_p->setPen(*_currentPen);
						_p->drawTextItem(QPointF(x.toReal(), textY), gf);
#ifdef Q_OS_MAC
						_p->restore();
#else // Q_OS_MAC
						if (clippingEnabled) {
							_p->setClipRegion(clippingRegion);
						} else {
							_p->setClipping(false);
						}
#endif // Q_OS_MAC
					} else {
						_p->setPen(*_currentPenSelected);
						_p->drawTextItem(QPointF(x.toReal(), textY), gf);
					}
				} else {
					_p->setPen(*_currentPen);
					_p->drawTextItem(QPointF(x.toReal(), textY), gf);
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
	}
	fillRectsFromRanges();
	return !_elidedLine;
}

FixedRange Renderer::findSelectEmojiRange(
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
			const auto gwidth = gf.glyphs.effectiveAdvance(g - lczero);
			// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
			int ch2 = ch + 1;
			while ((ch2 < itemL) && (g == gf.logClusters[ch2])) {
				++ch2;
			}
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
	if (range.empty()) {
		return;
	}
	const auto left = range.from.toInt();
	const auto width = range.till.toInt() - left;
	_p->fillRect(left, _y + _yDelta, width, _fontHeight, _palette->selectBg);
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
	const auto y = _y + _yDelta;
	for (const auto &range : ranges) {
		auto from = range.from.toInt();
		auto till = range.till.toInt();
		if (from <= lastTill) {
			auto &last = rects.back();
			from = std::min(from, last.x());
			till = std::max(till, last.x() + last.width());
			last = { from, y, till - from, _fontHeight };
		} else {
			rects.push_back({ from, y, till - from, _fontHeight });
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
			int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
			if (lineStart + lineLength < si.position + itemLength) {
				itemEnd = lineStart + lineLength;
				glyphsEnd = logClusters[itemEnd - si.position];
			} else {
				itemEnd = si.position + itemLength;
				glyphsEnd = si.num_glyphs;
			}

			for (auto g = glyphsStart; g < glyphsEnd; ++g) {
				auto adv = glyphs.effectiveAdvance(g);
				if (_wLeft < elisionWidth + adv) {
					_wLeft -= elisionWidth;

					auto pos = itemStart;
					while (pos < itemEnd && logClusters[pos - si.position] < g) {
						++pos;
					}

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
	const auto newFont = WithFlags(usedFont, flags);
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
					_quoteLinkPenOverride = QPen(_quoteBlockquoteCache->outlines[0]);
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
		} else if (isMono) {
			_currentPen = &_palette->monoFg->p;
			_currentPenSelected = &_palette->selectMonoFg->p;
		} else if (block->linkIndex()) {
			if (_quote && _quote->blockquote && _quoteBlockquoteCache) {
				_quoteLinkPenOverride = QPen(_quoteBlockquoteCache->outlines[0]);
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
	return (spoilerLink || !block->linkIndex() || !_t->_extended)
		? spoilerLink
		: _t->_extended->links[block->linkIndex() - 1];
}

} // namespace Ui::Text
