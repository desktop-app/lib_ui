// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_renderer.h"

#include "ui/text/text_extended_data.h"
#include "styles/style_basic.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <private/qharfbuzz_p.h>
#endif // Qt < 6.0.0

namespace Ui::Text {
namespace {

// COPIED FROM qtextengine.cpp AND MODIFIED

struct BidiStatus {
	BidiStatus() {
		eor = QChar::DirON;
		lastStrong = QChar::DirON;
		last = QChar::DirON;
		dir = QChar::DirON;
	}
	QChar::Direction eor;
	QChar::Direction lastStrong;
	QChar::Direction last;
	QChar::Direction dir;
};

enum { _MaxBidiLevel = 61 };
enum { _MaxItemLength = 4096 };

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

struct Renderer::BidiControl {
	inline BidiControl(bool rtl)
		: base(rtl ? 1 : 0), level(rtl ? 1 : 0) {}

	inline void embed(bool rtl, bool o = false) {
		unsigned int toAdd = 1;
		if ((level % 2 != 0) == rtl) {
			++toAdd;
		}
		if (level + toAdd <= _MaxBidiLevel) {
			ctx[cCtx].level = level;
			ctx[cCtx].override = override;
			cCtx++;
			override = o;
			level += toAdd;
		}
	}
	inline bool canPop() const { return cCtx != 0; }
	inline void pdf() {
		Q_ASSERT(cCtx);
		--cCtx;
		level = ctx[cCtx].level;
		override = ctx[cCtx].override;
	}

	inline QChar::Direction basicDirection() const {
		return (base ? QChar::DirR : QChar::DirL);
	}
	inline unsigned int baseLevel() const {
		return base;
	}
	inline QChar::Direction direction() const {
		return ((level % 2) ? QChar::DirR : QChar::DirL);
	}

	struct {
		unsigned int level = 0;
		bool override = false;
	} ctx[_MaxBidiLevel];
	unsigned int cCtx = 0;
	const unsigned int base;
	unsigned int level;
	bool override = false;
};

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
			context.availableWidth,
			_t->_st->font->height,
			context.elisionHeight,
			context.elisionRemoveFromEnd,
			context.elisionOneLine,
			context.elisionBreakEverywhere);
	_breakEverywhere = _geometry.breakEverywhere;
	_spoilerCache = context.spoiler;
	_selection = context.selection;
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

	_lineHeight = 0;
	_fontHeight = _t->_st->font->height;
	auto last_rBearing = QFixed(0);
	_last_rPadding = QFixed(0);

	const auto guard = gsl::finally([&] {
		if (_p) {
			paintSpoilerRects();
		}
	});

	auto blockIndex = 0;
	bool longWordLine = true;
	auto e = _t->_blocks.cend();
	for (auto i = _t->_blocks.cbegin(); i != e; ++i, ++blockIndex) {
		auto b = i->get();
		auto _btype = b->type();
		auto blockHeight = CountBlockHeight(b, _t->_st);

		if (_btype == TextBlockType::Newline) {
			if (!_lineHeight) {
				_lineHeight = blockHeight;
			}
			const auto qindex = static_cast<const NewlineBlock*>(b)->quoteIndex();
			const auto changed = (_quoteIndex != qindex);
			fillParagraphBg(changed ? _quotePadding.bottom() : 0);
			if (!drawLine((*i)->position(), i, e)) {
				return;
			}

			_y += _lineHeight;
			_lineHeight = 0;

			last_rBearing = 0;
			_last_rPadding = 0;

			initNextParagraph(
				i + 1,
				qindex,
				static_cast<const NewlineBlock*>(b)->paragraphDirection());

			longWordLine = true;
			continue;
		}

		auto b__f_rbearing = b->f_rbearing();
		auto newWidthLeft = _wLeft - last_rBearing - (_last_rPadding + b->f_width() - b__f_rbearing);
		if (newWidthLeft >= 0) {
			last_rBearing = b__f_rbearing;
			_last_rPadding = b->f_rpadding();
			_wLeft = newWidthLeft;

			_lineHeight = qMax(_lineHeight, blockHeight);

			longWordLine = false;
			continue;
		}

		if (_btype == TextBlockType::Text) {
			auto t = static_cast<const TextBlock*>(b);
			if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
				_last_rPadding += b->f_rpadding();

				_lineHeight = qMax(_lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			auto f_wLeft = _wLeft; // vars for saving state of the last word start
			auto f_lineHeight = _lineHeight; // f points to the last word-start element of t->_words
			for (auto j = t->_words.cbegin(), en = t->_words.cend(), f = j; j != en; ++j) {
				auto wordEndsHere = (j->f_width() >= 0);
				auto j_width = wordEndsHere ? j->f_width() : -j->f_width();

				auto newWidthLeft = _wLeft - last_rBearing - (_last_rPadding + j_width - j->f_rbearing());
				if (newWidthLeft >= 0) {
					last_rBearing = j->f_rbearing();
					_last_rPadding = j->f_rpadding();
					_wLeft = newWidthLeft;

					_lineHeight = qMax(_lineHeight, blockHeight);

					if (wordEndsHere) {
						longWordLine = false;
					}
					if (wordEndsHere || longWordLine) {
						f = j + 1;
						f_wLeft = _wLeft;
						f_lineHeight = _lineHeight;
					}
					continue;
				}

				if (_elidedLine) {
					_lineHeight = qMax(_lineHeight, blockHeight);
				} else if (f != j && !_breakEverywhere) {
					// word did not fit completely, so we roll back the state to the beginning of this long word
					j = f;
					_wLeft = f_wLeft;
					_lineHeight = f_lineHeight;
					j_width = (j->f_width() >= 0) ? j->f_width() : -j->f_width();
				}
				const auto lineEnd = !_elidedLine
					? j->position()
					: (j + 1 != en)
					? (j + 1)->position()
					: _t->countBlockEnd(i, e);
				fillParagraphBg(0);
				if (!drawLine(lineEnd, i, e)) {
					return;
				}
				_y += _lineHeight;
				_lineHeight = qMax(0, blockHeight);
				_lineStart = j->position();
				_lineStartBlock = blockIndex;
				_paragraphWidthRemaining -= (_lineWidth - _wLeft) - _last_rPadding + last_rBearing;
				initNextLine();

				last_rBearing = j->f_rbearing();
				_last_rPadding = j->f_rpadding();
				_wLeft -= j_width - last_rBearing;

				longWordLine = !wordEndsHere;
				f = j + 1;
				f_wLeft = _wLeft;
				f_lineHeight = _lineHeight;
			}
			continue;
		}

		if (_elidedLine) {
			_lineHeight = qMax(_lineHeight, blockHeight);
		}
		const auto lineEnd = !_elidedLine
			? b->position()
			: _t->countBlockEnd(i, e);
		fillParagraphBg(0);
		if (!drawLine(lineEnd, i, e)) {
			return;
		}
		_y += _lineHeight;
		_lineHeight = qMax(0, blockHeight);
		_lineStart = b->position();
		_lineStartBlock = blockIndex;
		_paragraphWidthRemaining -= (_lineWidth - _wLeft) - _last_rPadding + last_rBearing;
		initNextLine();

		last_rBearing = b__f_rbearing;
		_last_rPadding = b->f_rpadding();
		_wLeft -= b->f_width() - last_rBearing;

		longWordLine = true;
		continue;
	}
	if (_lineStart < _t->_text.size()) {
		fillParagraphBg(_quotePadding.bottom());
		if (!drawLine(_t->_text.size(), e, e)) {
			return;
		}
	}
	if (!_p && _lookupSymbol) {
		_lookupResult.symbol = _t->_text.size();
		_lookupResult.afterSymbol = false;
	}
}

void Renderer::fillParagraphBg(int paddingBottom) {
	if (_quote) {
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
			});
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
	return _lookupResult;
}

crl::time Renderer::now() const {
	if (!_cachedNow) {
		_cachedNow = crl::now();
	}
	return _cachedNow;
}

void Renderer::initNextParagraph(
		String::TextBlocks::const_iterator i,
		int16 paragraphIndex,
		Qt::LayoutDirection direction) {
	_paragraphDirection = (direction == Qt::LayoutDirectionAuto)
		? style::LayoutDirection()
		: direction;
	_paragraphStartBlock = i;
	_paragraphWidthRemaining = 0;
	if (_quoteIndex != paragraphIndex) {
		_y += _quotePadding.bottom();
		_quoteIndex = paragraphIndex;
		_quote = _t->quoteByIndex(paragraphIndex);
		_quotePadding = _t->quotePadding(_quote);
		_quoteTop = _quoteLineTop = _y;
		_y += _quotePadding.top();
		_quotePadding.setTop(0);
		_quoteDirection = _paragraphDirection;
	}
	const auto e = _t->_blocks.cend();
	if (i == e) {
		_lineStart = _paragraphStart = _t->_text.size();
		_lineStartBlock = _t->_blocks.size();
		_paragraphLength = 0;
	} else {
		_lineStart = _paragraphStart = (*i)->position();
		_lineStartBlock = i - _t->_blocks.cbegin();

		auto last_rPadding = QFixed(0);
		auto last_rBearing = QFixed(0);
		for (; i != e; ++i) {
			if ((*i)->type() == TextBlockType::Newline) {
				break;
			}
			const auto rBearing = (*i)->f_rbearing();
			_paragraphWidthRemaining += last_rBearing
				+ last_rPadding
				+ (*i)->f_width()
				- rBearing;
			last_rBearing = rBearing;
		}
		_paragraphLength = ((i == e)
			? _t->_text.size()
			: (*i)->position())
			- _paragraphStart;
	}
	_paragraphAnalysis.resize(0);
	_paragraphWidthRemaining += _quotePadding.left() + _quotePadding.right();
	initNextLine();
}

void Renderer::initNextLine() {
	const auto line = _geometry.layout({
		.left = 0,
		.top = (_y - _startTop),
		.width = _paragraphWidthRemaining.ceil().toInt(),
	});
	_quoteLineTop += _startTop + line.top - _y;
	_x = _startLeft + line.left + _quotePadding.left();
	_y = _startTop + line.top;
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
	_wLeft = _lineWidth;
	_elidedLine = line.elided;
}

void Renderer::initParagraphBidi() {
	if (!_paragraphLength || !_paragraphAnalysis.isEmpty()) {
		return;
	}

	String::TextBlocks::const_iterator i = _paragraphStartBlock, e = _t->_blocks.cend(), n = i + 1;

	bool ignore = false;
	bool rtl = (_paragraphDirection == Qt::RightToLeft);
	if (!ignore && !rtl) {
		ignore = true;
		const ushort *start = reinterpret_cast<const ushort*>(_str) + _paragraphStart;
		const ushort *curr = start;
		const ushort *end = start + _paragraphLength;
		while (curr < end) {
			while (n != e && (*n)->position() <= _paragraphStart + (curr - start)) {
				i = n;
				++n;
			}
			const auto type = (*i)->type();
			if (type != TextBlockType::Emoji
				&& type != TextBlockType::CustomEmoji
				&& *curr >= 0x590) {
				ignore = false;
				break;
			}
			++curr;
		}
	}

	_paragraphAnalysis.resize(_paragraphLength);
	QScriptAnalysis *analysis = _paragraphAnalysis.data();

	BidiControl control(rtl);

	_paragraphHasBidi = false;
	if (ignore) {
		memset(analysis, 0, _paragraphLength * sizeof(QScriptAnalysis));
		if (rtl) {
			for (int i = 0; i < _paragraphLength; ++i)
				analysis[i].bidiLevel = 1;
			_paragraphHasBidi = true;
		}
	} else {
		_paragraphHasBidi = eBidiItemize(analysis, control);
	}
}

bool Renderer::drawLine(uint16 _lineEnd, const String::TextBlocks::const_iterator &_endBlockIter, const String::TextBlocks::const_iterator &_end) {
	_yDelta = (_lineHeight - _fontHeight) / 2;
	if (_yTo >= 0 && (_y + _yDelta >= _yTo || _y >= _yTo)) {
		return false;
	}
	if (_y + _yDelta + _fontHeight <= _yFrom) {
		if (_lookupSymbol) {
			_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
			_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
		}
		return !_elidedLine;
	}

	// Trimming pending spaces, because they sometimes don't fit on the line.
	// They also are not counted in the line width, they're in the right padding.
	// Line width is a sum of block / word widths and paddings between them, without trailing one.
	auto trimmedLineEnd = _lineEnd;
	for (; trimmedLineEnd > _lineStart; --trimmedLineEnd) {
		auto ch = _t->_text[trimmedLineEnd - 1];
		if (ch != QChar::Space && ch != QChar::LineFeed) {
			break;
		}
	}

	auto _endBlock = (_endBlockIter == _end) ? nullptr : _endBlockIter->get();
	if (_elidedLine) {
		// If we decided to draw the last line elided only because of the skip block
		// that did not fit on this line, we just draw the line till the very end.
		// Skip block is ignored in the elided lines, instead "removeFromEnd" is used.
		if (_endBlock && _endBlock->type() == TextBlockType::Skip) {
			_endBlock = nullptr;
		}
		if (!_endBlock) {
			_elidedLine = false;
		}
	}

	auto blockIndex = _lineStartBlock;
	auto currentBlock = _t->_blocks[blockIndex].get();
	auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

	const auto extendLeft = (currentBlock->position() < _lineStart)
		? qMin(_lineStart - currentBlock->position(), 2)
		: 0;
	_localFrom = _lineStart - extendLeft;
	const auto extendedLineEnd = (_endBlock && _endBlock->position() < trimmedLineEnd && !_elidedLine)
		? qMin(uint16(trimmedLineEnd + 2), _t->countBlockEnd(_endBlockIter, _end))
		: trimmedLineEnd;

	auto lineText = _t->_text.mid(_localFrom, extendedLineEnd - _localFrom);
	auto lineStart = extendLeft;
	auto lineLength = trimmedLineEnd - _lineStart;

	if (_elidedLine) {
		initParagraphBidi();
		prepareElidedLine(lineText, lineStart, lineLength, _endBlock);
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
					_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
					_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
					//						_lookupResult.uponSymbol = ((_lookupX >= _x) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockType::Skip)) ? true : false;
				} else {
					_lookupResult.symbol = _lineStart;
					_lookupResult.afterSymbol = false;
					//						_lookupResult.uponSymbol = ((_lookupX >= _x) && (_lineStart > 0)) ? true : false;
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
				//					_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (_lineStart > 0)) ? true : false;
			} else {
				_lookupResult.symbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
				_lookupResult.afterSymbol = (_lineEnd > _lineStart) ? true : false;
				//					_lookupResult.uponSymbol = ((_lookupX < _x + _w) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockType::Skip)) ? true : false;
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
			&& (!_endBlock || _endBlock->type() != TextBlockType::Skip);

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
	QStackTextEngine engine(lineText, _f->f);
	engine.option.setTextDirection(_paragraphDirection);
	_e = &engine;

	eItemize();

	QScriptLine line;
	line.from = lineStart;
	line.length = lineLength;
	eShapeLine(line);

	int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0;
	if (!nItems) {
		return !_elidedLine;
	}

	int skipIndex = -1;
	QVarLengthArray<int> visualOrder(nItems);
	QVarLengthArray<uchar> levels(nItems);
	for (int i = 0; i < nItems; ++i) {
		auto &si = engine.layoutData->items[firstItem + i];
		while (nextBlock && nextBlock->position() <= _localFrom + si.position) {
			currentBlock = nextBlock;
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
		}
		auto _type = currentBlock->type();
		if (_type == TextBlockType::Skip) {
			levels[i] = si.analysis.bidiLevel = 0;
			skipIndex = i;
		} else {
			levels[i] = si.analysis.bidiLevel;
		}
		if (si.analysis.flags == QScriptAnalysis::Object) {
			if (_type == TextBlockType::Emoji
				|| _type == TextBlockType::CustomEmoji
				|| _type == TextBlockType::Skip) {
				si.width = currentBlock->f_width()
					+ (nextBlock == _endBlock && (!nextBlock || nextBlock->position() >= trimmedLineEnd)
						? 0
						: currentBlock->f_rpadding());
			}
		}
	}
	QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());
	if (style::RightToLeft() && skipIndex == nItems - 1) {
		for (int32 i = nItems; i > 1;) {
			--i;
			visualOrder[i] = visualOrder[i - 1];
		}
		visualOrder[0] = skipIndex;
	}

	blockIndex = _lineStartBlock;
	currentBlock = _t->_blocks[blockIndex].get();
	nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

	int32 textY = _y + _yDelta + _t->_st->font->ascent, emojiY = (_t->_st->font->height - st::emojiSize) / 2;

	applyBlockProperties(currentBlock);
	for (int i = 0; i < nItems; ++i) {
		const auto item = firstItem + visualOrder[i];
		const auto isLastItem = (item == lastItem);
		const auto &si = engine.layoutData->items.at(item);
		const auto rtl = (si.analysis.bidiLevel % 2);

		while (blockIndex > _lineStartBlock + 1 && _t->_blocks[blockIndex - 1]->position() > _localFrom + si.position) {
			nextBlock = currentBlock;
			currentBlock = _t->_blocks[--blockIndex - 1].get();
			applyBlockProperties(currentBlock);
		}
		while (nextBlock && nextBlock->position() <= _localFrom + si.position) {
			currentBlock = nextBlock;
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			applyBlockProperties(currentBlock);
		}
		if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
			TextBlockType _type = currentBlock->type();
			if (!_p && _lookupX >= x && _lookupX < x + si.width) { // _lookupRequest
				if (_lookupLink) {
					if (_lookupY >= _y + _yDelta && _lookupY < _y + _yDelta + _fontHeight) {
						if (const auto link = lookupLink(currentBlock)) {
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
					auto chFrom = _str + currentBlock->position();
					auto chTo = chFrom + ((nextBlock ? nextBlock->position() : _t->_text.size()) - currentBlock->position());
					auto spacesWidth = (si.width - currentBlock->f_width());
					auto spacesCount = 0;
					while (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space) {
						++spacesCount;
						--chTo;
					}
					if (spacesCount > 0) { // Check if we're over a space.
						if (rtl) {
							if (_lookupX < x + spacesWidth) {
								_lookupResult.symbol = (chTo - _str); // up to a space, included, rtl
								_lookupResult.afterSymbol = (_lookupX < x + (spacesWidth / 2)) ? true : false;
								return false;
							}
						} else if (_lookupX >= x + si.width - spacesWidth) {
							_lookupResult.symbol = (chTo - _str); // up to a space, inclided, ltr
							_lookupResult.afterSymbol = (_lookupX >= x + si.width - spacesWidth + (spacesWidth / 2)) ? true : false;
							return false;
						}
					}
					if (_lookupX < x + (rtl ? (si.width - currentBlock->f_width()) : 0) + (currentBlock->f_width() / 2)) {
						_lookupResult.symbol = ((rtl && chTo > chFrom) ? (chTo - 1) : chFrom) - _str;
						_lookupResult.afterSymbol = (rtl && chTo > chFrom) ? true : false;
					} else {
						_lookupResult.symbol = ((rtl || chTo <= chFrom) ? chFrom : (chTo - 1)) - _str;
						_lookupResult.afterSymbol = (rtl || chTo <= chFrom) ? false : true;
					}
				}
				return false;
			} else if (_p && (_type == TextBlockType::Emoji || _type == TextBlockType::CustomEmoji)) {
				auto glyphX = x;
				auto spacesWidth = (si.width - currentBlock->f_width());
				if (rtl) {
					glyphX += spacesWidth;
				}
				FixedRange fillSelect;
				FixedRange fillSpoiler;
				if (_background.selectActiveBlock) {
					fillSelect = { x, x + si.width };
				} else if (_localFrom + si.position < _selection.to) {
					auto chFrom = _str + currentBlock->position();
					auto chTo = chFrom + ((nextBlock ? nextBlock->position() : _t->_text.size()) - currentBlock->position());
					if (_localFrom + si.position >= _selection.from) { // could be without space
						if (chTo == chFrom || (chTo - 1)->unicode() != QChar::Space || _selection.to >= (chTo - _str)) {
							fillSelect = { x, x + si.width };
						} else { // or with space
							fillSelect = { glyphX, glyphX + currentBlock->f_width() };
						}
					} else if (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space && (chTo - 1 - _str) >= _selection.from) {
						if (rtl) { // rtl space only
							fillSelect = { x, glyphX };
						} else { // ltr space only
							fillSelect = { x + currentBlock->f_width(), x + si.width };
						}
					}
				}
				const auto hasSpoiler = _background.spoiler
					&& (_spoilerOpacity > 0.);
				if (hasSpoiler) {
					fillSpoiler = { x, x + si.width };
				}
				fillSelectRange(fillSelect);
				const auto opacity = _p->opacity();
				if (!hasSpoiler || _spoilerOpacity < 1.) {
					if (hasSpoiler) {
						_p->setOpacity(opacity * (1. - _spoilerOpacity));
					}
					const auto x = (glyphX + st::emojiPadding).toInt();
					const auto y = _y + _yDelta + emojiY;
					if (_type == TextBlockType::Emoji) {
						Emoji::Draw(
							*_p,
							static_cast<const EmojiBlock*>(currentBlock)->_emoji,
							Emoji::GetSizeNormal(),
							x,
							y);
					} else if (const auto custom = static_cast<const CustomEmojiBlock*>(currentBlock)->_custom.get()) {
						const auto selected = (fillSelect.from <= glyphX)
							&& (fillSelect.till > glyphX);
						const auto color = (selected
							? _currentPenSelected
							: _currentPen)->color();
						if (!_customEmojiSize) {
							_customEmojiSize = AdjustCustomEmojiSize(st::emojiSize);
							_customEmojiSkip = (st::emojiSize - _customEmojiSize) / 2;
							_customEmojiContext = CustomEmoji::Context{
								.textColor = color,
								.now = now(),
								.paused = _pausedEmoji,
							};
						} else {
							_customEmojiContext->textColor = color;
						}
						_customEmojiContext->position = {
							x + _customEmojiSkip,
							y + _customEmojiSkip,
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
					pushSpoilerRange(fillSpoiler, fillSelect, isElidedItem);
				}
			//} else if (_p && currentBlock->type() == TextBlockSkip) { // debug
			//	_p->fillRect(QRect(x.toInt(), _y, currentBlock->width(), static_cast<SkipBlock*>(currentBlock)->height()), QColor(0, 0, 0, 32));
			}
			x += si.width;
			continue;
		}

		unsigned short *logClusters = engine.logClusters(&si);
		QGlyphLayout glyphs = engine.shapedGlyphs(&si);

		int itemStart = qMax(line.from, si.position), itemEnd;
		int itemLength = engine.length(item);
		int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
		if (line.from + line.length < si.position + itemLength) {
			itemEnd = line.from + line.length;
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
					if (const auto link = lookupLink(currentBlock)) {
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
			gf.f = &_e->fnt;
			gf.chars = engine.layoutData->string.unicode() + itemStart;
			gf.num_chars = itemEnd - itemStart;
			gf.fontEngine = engine.fontEngine(si);
			gf.logClusters = logClusters + itemStart - si.position;
			gf.width = itemWidth;
			gf.justified = false;
			InitTextItemWithScriptItem(gf, si);

			auto itemRange = FixedRange{ x, x + itemWidth };
			auto fillSelect = FixedRange();
			auto hasSelected = false;
			auto hasNotSelected = true;
			auto selectedRect = QRect();
			if (_background.selectActiveBlock) {
				fillSelect = itemRange;
				fillSelectRange(fillSelect);
			} else if (_localFrom + itemStart < _selection.to && _localFrom + itemEnd > _selection.from) {
				hasSelected = true;
				auto selX = x;
				auto selWidth = itemWidth;
				if (_localFrom + itemStart >= _selection.from && _localFrom + itemEnd <= _selection.to) {
					hasNotSelected = false;
				} else {
					selWidth = 0;
					int itemL = itemEnd - itemStart;
					int selStart = _selection.from - (_localFrom + itemStart), selEnd = _selection.to - (_localFrom + itemStart);
					if (selStart < 0) selStart = 0;
					if (selEnd > itemL) selEnd = itemL;
					for (int ch = 0, g; ch < selEnd;) {
						g = logClusters[itemStart - si.position + ch];
						QFixed gwidth = glyphs.effectiveAdvance(g);
						// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
						int ch2 = ch + 1;
						while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
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
				selectedRect = QRect(selX.toInt(), _y + _yDelta, (selX + selWidth).toInt() - selX.toInt(), _fontHeight);
				fillSelect = { selX, selX + selWidth };
				fillSelectRange(fillSelect);
			}
			const auto hasSpoiler = _background.spoiler
				&& (_spoilerOpacity > 0.);
			const auto opacity = _p->opacity();
			const auto isElidedItem = (_indexOfElidedBlock == blockIndex)
				&& isLastItem;
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
					const auto elided = (_indexOfElidedBlock == blockIndex)
						? _f->elidew
						: 0;
					_p->setClipRect(
						QRect(
							(x + itemWidth).toInt() - elided,
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
				pushSpoilerRange(itemRange, fillSelect, isElidedItem);
			}
		}

		x += itemWidth;
	}
	fillSpoilerRects();
	return !_elidedLine;
}

void Renderer::fillSelectRange(FixedRange range) {
	if (range.empty()) {
		return;
	}
	const auto left = range.from.toInt();
	const auto width = range.till.toInt() - left;
	_p->fillRect(left, _y + _yDelta, width, _fontHeight, _palette->selectBg);
}

void Renderer::pushSpoilerRange(
		FixedRange range,
		FixedRange selected,
		bool isElidedItem) {
	if (!_background.spoiler || !_spoiler) {
		return;
	}
	const auto elided = isElidedItem ? _f->elidew : 0;
	range.till -= elided;
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

void Renderer::fillSpoilerRects() {
	fillSpoilerRects(_spoilerRects, _spoilerRanges);
	fillSpoilerRects(_spoilerSelectedRects, _spoilerSelectedRanges);
}

void Renderer::fillSpoilerRects(
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

void Renderer::elideSaveBlock(int32 blockIndex, const AbstractBlock *&_endBlock, int32 elideStart, int32 elideWidth) {
	if (_elideSavedBlock) {
		restoreAfterElided();
	}

	_elideSavedIndex = blockIndex;
	auto mutableText = const_cast<String*>(_t);
	_elideSavedBlock = std::move(mutableText->_blocks[blockIndex]);
	mutableText->_blocks[blockIndex] = Block::Text(
		_t->_st->font,
		_t->_text,
		elideStart, 0,
		(*_elideSavedBlock)->flags(),
		(*_elideSavedBlock)->linkIndex(),
		(*_elideSavedBlock)->colorIndex(),
		QFIXED_MAX);
	_blocksSize = blockIndex + 1;
	_endBlock = (blockIndex + 1 < _t->_blocks.size())
		? _t->_blocks[blockIndex + 1].get()
		: nullptr;
}

void Renderer::setElideBidi(int32 elideStart, int32 elideLen) {
	int32 newParLength = elideStart + elideLen - _paragraphStart;
	if (newParLength > _paragraphAnalysis.size()) {
		_paragraphAnalysis.resize(newParLength);
	}
	for (int32 i = elideLen; i > 0; --i) {
		_paragraphAnalysis[newParLength - i].bidiLevel
			= (_paragraphDirection == Qt::RightToLeft) ? 1 : 0;
	}
}

void Renderer::prepareElidedLine(
		QString &lineText,
		int32 lineStart,
		int32 &lineLength,
		const AbstractBlock *&_endBlock,
		int repeat) {
	_f = _t->_st->font;
	QStackTextEngine engine(lineText, _f->f);
	engine.option.setTextDirection(_paragraphDirection);
	_e = &engine;

	eItemize();

	auto blockIndex = _lineStartBlock;
	auto currentBlock = _t->_blocks[blockIndex].get();
	auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

	QScriptLine line;
	line.from = lineStart;
	line.length = lineLength;
	eShapeLine(line);

	auto elideWidth = _f->elidew;
	_wLeft = _lineWidth
		- _quotePadding.left()
		- _quotePadding.right()
		- elideWidth;

	int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0, i;

	for (i = 0; i < nItems; ++i) {
		QScriptItem &si(engine.layoutData->items[firstItem + i]);
		while (nextBlock && nextBlock->position() <= _localFrom + si.position) {
			currentBlock = nextBlock;
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
		}
		TextBlockType _type = currentBlock->type();
		if (si.analysis.flags == QScriptAnalysis::Object) {
			if (_type == TextBlockType::Emoji
				|| _type == TextBlockType::CustomEmoji
				|| _type == TextBlockType::Skip) {
				si.width = currentBlock->f_width() + currentBlock->f_rpadding();
			}
		}
		if (_type == TextBlockType::Emoji
			|| _type == TextBlockType::CustomEmoji
			|| _type == TextBlockType::Skip
			|| _type == TextBlockType::Newline) {
			if (_wLeft < si.width) {
				lineText = lineText.mid(0, currentBlock->position() - _localFrom) + kQEllipsis;
				lineLength = currentBlock->position() + kQEllipsis.size() - _lineStart;
				_selection.to = qMin(_selection.to, currentBlock->position());
				_indexOfElidedBlock = blockIndex + (nextBlock ? 1 : 0);
				setElideBidi(currentBlock->position(), kQEllipsis.size());
				elideSaveBlock(blockIndex - 1, _endBlock, currentBlock->position(), elideWidth);
				return;
			}
			_wLeft -= si.width;
		} else if (_type == TextBlockType::Text) {
			unsigned short *logClusters = engine.logClusters(&si);
			QGlyphLayout glyphs = engine.shapedGlyphs(&si);

			int itemStart = qMax(line.from, si.position), itemEnd;
			int itemLength = engine.length(firstItem + i);
			int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
			if (line.from + line.length < si.position + itemLength) {
				itemEnd = line.from + line.length;
				glyphsEnd = logClusters[itemEnd - si.position];
			} else {
				itemEnd = si.position + itemLength;
				glyphsEnd = si.num_glyphs;
			}

			for (auto g = glyphsStart; g < glyphsEnd; ++g) {
				auto adv = glyphs.effectiveAdvance(g);
				if (_wLeft < adv) {
					auto pos = itemStart;
					while (pos < itemEnd && logClusters[pos - si.position] < g) {
						++pos;
					}

					if (lineText.size() <= pos || repeat > 3) {
						lineText += kQEllipsis;
						lineLength = _localFrom + pos + kQEllipsis.size() - _lineStart;
						_selection.to = qMin(_selection.to, uint16(_localFrom + pos));
						_indexOfElidedBlock = blockIndex + (nextBlock ? 1 : 0);
						setElideBidi(_localFrom + pos, kQEllipsis.size());
						_blocksSize = blockIndex;
						_endBlock = nextBlock;
					} else {
						lineText = lineText.mid(0, pos);
						lineLength = _localFrom + pos - _lineStart;
						_blocksSize = blockIndex;
						_endBlock = nextBlock;
						prepareElidedLine(lineText, lineStart, lineLength, _endBlock, repeat + 1);
					}
					return;
				} else {
					_wLeft -= adv;
				}
			}
		}
	}

	int32 elideStart = _localFrom + lineText.size();
	_selection.to = qMin(_selection.to, uint16(elideStart));
	_indexOfElidedBlock = blockIndex + (nextBlock ? 1 : 0);
	setElideBidi(elideStart, kQEllipsis.size());

	lineText += kQEllipsis;
	lineLength += kQEllipsis.size();

	if (!repeat) {
		for (; blockIndex < _blocksSize && _t->_blocks[blockIndex].get() != _endBlock && _t->_blocks[blockIndex]->position() < elideStart; ++blockIndex) {
		}
		if (blockIndex < _blocksSize) {
			elideSaveBlock(blockIndex, _endBlock, elideStart, elideWidth);
		}
	}
}

void Renderer::restoreAfterElided() {
	if (_elideSavedBlock) {
		const_cast<String*>(_t)->_blocks[_elideSavedIndex] = std::move(*_elideSavedBlock);
	}
}

// COPIED FROM qtextengine.cpp AND MODIFIED
void Renderer::eAppendItems(QScriptAnalysis *analysis, int &start, int &stop, const BidiControl &control, QChar::Direction dir) {
	if (start > stop)
		return;

	int level = control.level;

	if(dir != QChar::DirON && !control.override) {
		// add level of run (cases I1 & I2)
		if(level % 2) {
			if(dir == QChar::DirL || dir == QChar::DirAN || dir == QChar::DirEN)
				level++;
		} else {
			if(dir == QChar::DirR)
				level++;
			else if(dir == QChar::DirAN || dir == QChar::DirEN)
				level += 2;
		}
	}

	QScriptAnalysis *s = analysis + start;
	const QScriptAnalysis *e = analysis + stop;
	while (s <= e) {
		s->bidiLevel = level;
		++s;
	}
	++stop;
	start = stop;
}

void Renderer::eShapeLine(const QScriptLine &line) {
	int item = _e->findItem(line.from);
	if (item == -1) {
		return;
	}

	auto end = _e->findItem(line.from + line.length - 1, item);
	auto blockIndex = _lineStartBlock;
	auto currentBlock = _t->_blocks[blockIndex].get();
	auto nextBlock = (++blockIndex < _blocksSize)
		? _t->_blocks[blockIndex].get()
		: nullptr;
	eSetFont(currentBlock);
	for (; item <= end; ++item) {
		QScriptItem &si = _e->layoutData->items[item];
		while (nextBlock && nextBlock->position() <= _localFrom + si.position) {
			currentBlock = nextBlock;
			nextBlock = (++blockIndex < _blocksSize)
				? _t->_blocks[blockIndex].get()
				: nullptr;
			eSetFont(currentBlock);
		}
		_e->shape(item);
	}
}

void Renderer::eSetFont(const AbstractBlock *block) {
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
	if (newFont != _f) {
		_f = (newFont->family() == _t->_st->font->family())
			? WithFlags(_t->_st->font, flags, newFont->flags())
			: newFont;
		_e->fnt = _f->f;
		_e->resetFontEngineCache();
	}
}

void Renderer::eItemize() {
	_e->validate();
	if (_e->layoutData->items.size())
		return;

	int length = _e->layoutData->string.length();
	if (!length)
		return;

	const ushort *string = reinterpret_cast<const ushort*>(_e->layoutData->string.unicode());

	auto blockIndex = _lineStartBlock;
	auto currentBlock = _t->_blocks[blockIndex].get();
	auto nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

	_e->layoutData->hasBidi = _paragraphHasBidi;
	auto analysis = _paragraphAnalysis.data() + (_localFrom - _paragraphStart);

	{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		QUnicodeTools::ScriptItemArray scriptItems;
		QUnicodeTools::initScripts(_e->layoutData->string, &scriptItems);
		for (int i = 0; i < scriptItems.length(); ++i) {
			const auto &item = scriptItems.at(i);
			int end = i < scriptItems.length() - 1 ? scriptItems.at(i + 1).position : length;
			for (int j = item.position; j < end; ++j)
				analysis[j].script = item.script;
		}
#else // Qt >= 6.0.0
		QVarLengthArray<uchar> scripts(length);
		QUnicodeTools::initScripts(string, length, scripts.data());
		for (int i = 0; i < length; ++i)
			analysis[i].script = scripts.at(i);
#endif // Qt < 6.0.0
	}

	blockIndex = _lineStartBlock;
	currentBlock = _t->_blocks[blockIndex].get();
	nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;

	auto start = string;
	auto end = start + length;
	while (start < end) {
		while (nextBlock && nextBlock->position() <= _localFrom + (start - string)) {
			currentBlock = nextBlock;
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
		}
		auto _type = currentBlock->type();
		if (_type == TextBlockType::Emoji
			|| _type == TextBlockType::CustomEmoji
			|| _type == TextBlockType::Skip) {
			analysis->script = QChar::Script_Common;
			analysis->flags = QScriptAnalysis::Object;
		} else {
			analysis->flags = QScriptAnalysis::None;
		}
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		analysis->script = hbscript_to_script(script_to_hbscript(analysis->script)); // retain the old behavior
#endif // Qt < 6.0.0
		++start;
		++analysis;
	}

	{
		auto i_string = &_e->layoutData->string;
		auto i_analysis = _paragraphAnalysis.data() + (_localFrom - _paragraphStart);
		auto i_items = &_e->layoutData->items;

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex].get();
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
		auto startBlock = currentBlock;

		if (!length) {
			return;
		}
		auto start = 0;
		auto end = start + length;
		for (int i = start + 1; i < end; ++i) {
			while (nextBlock && nextBlock->position() <= _localFrom + i) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex].get() : nullptr;
			}
			// According to the unicode spec we should be treating characters in the Common script
			// (punctuation, spaces, etc) as being the same script as the surrounding text for the
			// purpose of splitting up text. This is important because, for example, a fullstop
			// (0x2E) can be used to indicate an abbreviation and so must be treated as part of a
			// word.  Thus it must be passed along with the word in languages that have to calculate
			// word breaks. For example the thai word "[lookup-in-git]." has no word breaks
			// but the word "[lookup-too]" does.
			// Unfortuntely because we split up the strings for both wordwrapping and for setting
			// the font and because Japanese and Chinese are also aliases of the script "Common",
			// doing this would break too many things.  So instead we only pass the full stop
			// along, and nothing else.
			if (currentBlock == startBlock
				&& i_analysis[i].bidiLevel == i_analysis[start].bidiLevel
				&& i_analysis[i].flags == i_analysis[start].flags
				&& (i_analysis[i].script == i_analysis[start].script || i_string->at(i) == QLatin1Char('.'))
				//					&& i_analysis[i].flags < QScriptAnalysis::SpaceTabOrObject // only emojis are objects here, no tabs
				&& i - start < _MaxItemLength)
				continue;
			i_items->append(QScriptItem(start, i_analysis[start]));
			start = i;
			startBlock = currentBlock;
		}
		i_items->append(QScriptItem(start, i_analysis[start]));
	}
}

QChar::Direction Renderer::eSkipBoundryNeutrals(
		QScriptAnalysis *analysis,
		const ushort *unicode,
		int &sor,
		int &eor,
		BidiControl &control,
		String::TextBlocks::const_iterator i) {
	String::TextBlocks::const_iterator e = _t->_blocks.cend(), n = i + 1;

	QChar::Direction dir = control.basicDirection();
	int level = sor > 0 ? analysis[sor - 1].bidiLevel : control.level;
	while (sor <= _paragraphLength) {
		while (i != _paragraphStartBlock && (*i)->position() > _paragraphStart + sor) {
			n = i;
			--i;
		}
		while (n != e && (*n)->position() <= _paragraphStart + sor) {
			i = n;
			++n;
		}

		TextBlockType _itype = (*i)->type();
		if (eor == _paragraphLength)
			dir = control.basicDirection();
		else if (_itype == TextBlockType::Emoji
			|| _itype == TextBlockType::CustomEmoji)
			dir = QChar::DirCS;
		else if (_itype == TextBlockType::Skip)
			dir = QChar::DirCS;
		else
			dir = QChar::direction(unicode[sor]);
		// Keep skipping DirBN as if it doesn't exist
		if (dir != QChar::DirBN)
			break;
		analysis[sor++].bidiLevel = level;
	}

	eor = sor;

	return dir;
}

// creates the next QScript items.
bool Renderer::eBidiItemize(QScriptAnalysis *analysis, BidiControl &control) {
	bool rightToLeft = (control.basicDirection() == 1);
	bool hasBidi = rightToLeft;

	int sor = 0;
	int eor = -1;

	const ushort *unicode = reinterpret_cast<const ushort*>(_t->_text.unicode()) + _paragraphStart;
	int current = 0;

	QChar::Direction dir = rightToLeft ? QChar::DirR : QChar::DirL;
	BidiStatus status;

	String::TextBlocks::const_iterator i = _paragraphStartBlock, e = _t->_blocks.cend(), n = i + 1;

	QChar::Direction sdir;
	TextBlockType _stype = (*_paragraphStartBlock)->type();
	if (_stype == TextBlockType::Emoji || _stype == TextBlockType::CustomEmoji)
		sdir = QChar::DirCS;
	else if (_stype == TextBlockType::Skip)
		sdir = QChar::DirCS;
	else
		sdir = QChar::direction(*unicode);
	if (sdir != QChar::DirL && sdir != QChar::DirR && sdir != QChar::DirEN && sdir != QChar::DirAN)
		sdir = QChar::DirON;
	else
		dir = QChar::DirON;

	status.eor = sdir;
	status.lastStrong = rightToLeft ? QChar::DirR : QChar::DirL;
	status.last = status.lastStrong;
	status.dir = sdir;

	while (current <= _paragraphLength) {
		while (n != e && (*n)->position() <= _paragraphStart + current) {
			i = n;
			++n;
		}

		QChar::Direction dirCurrent;
		TextBlockType _itype = (*i)->type();
		if (current == (int)_paragraphLength)
			dirCurrent = control.basicDirection();
		else if (_itype == TextBlockType::Emoji
			|| _itype == TextBlockType::CustomEmoji)
			dirCurrent = QChar::DirCS;
		else if (_itype == TextBlockType::Skip)
			dirCurrent = QChar::DirCS;
		else
			dirCurrent = QChar::direction(unicode[current]);

		switch (dirCurrent) {

			// embedding and overrides (X1-X9 in the BiDi specs)
		case QChar::DirRLE:
		case QChar::DirRLO:
		case QChar::DirLRE:
		case QChar::DirLRO:
		{
			bool rtl = (dirCurrent == QChar::DirRLE || dirCurrent == QChar::DirRLO);
			hasBidi |= rtl;
			bool override = (dirCurrent == QChar::DirLRO || dirCurrent == QChar::DirRLO);

			unsigned int level = control.level + 1;
			if ((level % 2 != 0) == rtl) ++level;
			if (level < _MaxBidiLevel) {
				eor = current - 1;
				eAppendItems(analysis, sor, eor, control, dir);
				eor = current;
				control.embed(rtl, override);
				QChar::Direction edir = (rtl ? QChar::DirR : QChar::DirL);
				dir = status.eor = edir;
				status.lastStrong = edir;
			}
			break;
		}
		case QChar::DirPDF:
		{
			if (control.canPop()) {
				if (dir != control.direction()) {
					eor = current - 1;
					eAppendItems(analysis, sor, eor, control, dir);
					dir = control.direction();
				}
				eor = current;
				eAppendItems(analysis, sor, eor, control, dir);
				control.pdf();
				dir = QChar::DirON; status.eor = QChar::DirON;
				status.last = control.direction();
				if (control.override)
					dir = control.direction();
				else
					dir = QChar::DirON;
				status.lastStrong = control.direction();
			}
			break;
		}

		// strong types
		case QChar::DirL:
			if (dir == QChar::DirON)
				dir = QChar::DirL;
			switch (status.last)
			{
			case QChar::DirL:
				eor = current; status.eor = QChar::DirL; break;
			case QChar::DirR:
			case QChar::DirAL:
			case QChar::DirEN:
			case QChar::DirAN:
				if (eor >= 0) {
					eAppendItems(analysis, sor, eor, control, dir);
					status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
				} else {
					eor = current; status.eor = dir;
				}
				break;
			case QChar::DirES:
			case QChar::DirET:
			case QChar::DirCS:
			case QChar::DirBN:
			case QChar::DirB:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
				if (dir != QChar::DirL) {
					//last stuff takes embedding dir
					if (control.direction() == QChar::DirR) {
						if (status.eor != QChar::DirR) {
							// AN or EN
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirON;
							dir = QChar::DirR;
						}
						eor = current - 1;
						eAppendItems(analysis, sor, eor, control, dir);
						status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
					} else {
						if (status.eor != QChar::DirL) {
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirON;
							dir = QChar::DirL;
						} else {
							eor = current; status.eor = QChar::DirL; break;
						}
					}
				} else {
					eor = current; status.eor = QChar::DirL;
				}
			default:
				break;
			}
			status.lastStrong = QChar::DirL;
			break;
		case QChar::DirAL:
		case QChar::DirR:
			hasBidi = true;
			if (dir == QChar::DirON) dir = QChar::DirR;
			switch (status.last)
			{
			case QChar::DirL:
			case QChar::DirEN:
			case QChar::DirAN:
				if (eor >= 0)
					eAppendItems(analysis, sor, eor, control, dir);
				// fall through
			case QChar::DirR:
			case QChar::DirAL:
				dir = QChar::DirR; eor = current; status.eor = QChar::DirR; break;
			case QChar::DirES:
			case QChar::DirET:
			case QChar::DirCS:
			case QChar::DirBN:
			case QChar::DirB:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
				if (status.eor != QChar::DirR && status.eor != QChar::DirAL) {
					//last stuff takes embedding dir
					if (control.direction() == QChar::DirR
						|| status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
						eAppendItems(analysis, sor, eor, control, dir);
						dir = QChar::DirR; status.eor = QChar::DirON;
						eor = current;
					} else {
						eor = current - 1;
						eAppendItems(analysis, sor, eor, control, dir);
						dir = QChar::DirR; status.eor = QChar::DirON;
					}
				} else {
					eor = current; status.eor = QChar::DirR;
				}
			default:
				break;
			}
			status.lastStrong = dirCurrent;
			break;

			// weak types:

		case QChar::DirNSM:
			if (eor == current - 1)
				eor = current;
			break;
		case QChar::DirEN:
			// if last strong was AL change EN to AN
			if (status.lastStrong != QChar::DirAL) {
				if (dir == QChar::DirON) {
					if (status.lastStrong == QChar::DirL)
						dir = QChar::DirL;
					else
						dir = QChar::DirEN;
				}
				switch (status.last)
				{
				case QChar::DirET:
					if (status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
						eAppendItems(analysis, sor, eor, control, dir);
						status.eor = QChar::DirON;
						dir = QChar::DirAN;
					}
					[[fallthrough]];
				case QChar::DirEN:
				case QChar::DirL:
					eor = current;
					status.eor = dirCurrent;
					break;
				case QChar::DirR:
				case QChar::DirAL:
				case QChar::DirAN:
					if (eor >= 0)
						eAppendItems(analysis, sor, eor, control, dir);
					else
						eor = current;
					status.eor = QChar::DirEN;
					dir = QChar::DirAN;
					break;
				case QChar::DirES:
				case QChar::DirCS:
					if (status.eor == QChar::DirEN || dir == QChar::DirAN) {
						eor = current; break;
					}
					[[fallthrough]];
				case QChar::DirBN:
				case QChar::DirB:
				case QChar::DirS:
				case QChar::DirWS:
				case QChar::DirON:
					if (status.eor == QChar::DirR) {
						// neutrals go to R
						eor = current - 1;
						eAppendItems(analysis, sor, eor, control, dir);
						status.eor = QChar::DirEN;
						dir = QChar::DirAN;
					} else if (status.eor == QChar::DirL ||
						(status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
						eor = current; status.eor = dirCurrent;
					} else {
						// numbers on both sides, neutrals get right to left direction
						if (dir != QChar::DirL) {
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirON;
							eor = current - 1;
							dir = QChar::DirR;
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirON;
							dir = QChar::DirAN;
						} else {
							eor = current; status.eor = dirCurrent;
						}
					}
					[[fallthrough]];
				default:
					break;
				}
				break;
			}
			[[fallthrough]];
		case QChar::DirAN:
			hasBidi = true;
			dirCurrent = QChar::DirAN;
			if (dir == QChar::DirON) dir = QChar::DirAN;
			switch (status.last)
			{
			case QChar::DirL:
			case QChar::DirAN:
				eor = current; status.eor = QChar::DirAN;
				break;
			case QChar::DirR:
			case QChar::DirAL:
			case QChar::DirEN:
				if (eor >= 0) {
					eAppendItems(analysis, sor, eor, control, dir);
				} else {
					eor = current;
				}
				dir = QChar::DirAN; status.eor = QChar::DirAN;
				break;
			case QChar::DirCS:
				if (status.eor == QChar::DirAN) {
					eor = current; break;
				}
				[[fallthrough]];
			case QChar::DirES:
			case QChar::DirET:
			case QChar::DirBN:
			case QChar::DirB:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
				if (status.eor == QChar::DirR) {
					// neutrals go to R
					eor = current - 1;
					eAppendItems(analysis, sor, eor, control, dir);
					status.eor = QChar::DirAN;
					dir = QChar::DirAN;
				} else if (status.eor == QChar::DirL ||
					(status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
					eor = current; status.eor = dirCurrent;
				} else {
					// numbers on both sides, neutrals get right to left direction
					if (dir != QChar::DirL) {
						eAppendItems(analysis, sor, eor, control, dir);
						status.eor = QChar::DirON;
						eor = current - 1;
						dir = QChar::DirR;
						eAppendItems(analysis, sor, eor, control, dir);
						status.eor = QChar::DirAN;
						dir = QChar::DirAN;
					} else {
						eor = current; status.eor = dirCurrent;
					}
				}
				[[fallthrough]];
			default:
				break;
			}
			break;
		case QChar::DirES:
		case QChar::DirCS:
			break;
		case QChar::DirET:
			if (status.last == QChar::DirEN) {
				dirCurrent = QChar::DirEN;
				eor = current; status.eor = dirCurrent;
			}
			break;

			// boundary neutrals should be ignored
		case QChar::DirBN:
			break;
			// neutrals
		case QChar::DirB:
			// ### what do we do with newline and paragraph separators that come to here?
			break;
		case QChar::DirS:
			// ### implement rule L1
			break;
		case QChar::DirWS:
		case QChar::DirON:
			break;
		default:
			break;
		}

		if (current >= (int)_paragraphLength) break;

		// set status.last as needed.
		switch (dirCurrent) {
		case QChar::DirET:
		case QChar::DirES:
		case QChar::DirCS:
		case QChar::DirS:
		case QChar::DirWS:
		case QChar::DirON:
			switch (status.last)
			{
			case QChar::DirL:
			case QChar::DirR:
			case QChar::DirAL:
			case QChar::DirEN:
			case QChar::DirAN:
				status.last = dirCurrent;
				break;
			default:
				status.last = QChar::DirON;
			}
			break;
		case QChar::DirNSM:
		case QChar::DirBN:
			// ignore these
			break;
		case QChar::DirLRO:
		case QChar::DirLRE:
			status.last = QChar::DirL;
			break;
		case QChar::DirRLO:
		case QChar::DirRLE:
			status.last = QChar::DirR;
			break;
		case QChar::DirEN:
			if (status.last == QChar::DirL) {
				status.last = QChar::DirL;
				break;
			}
			[[fallthrough]];
		default:
			status.last = dirCurrent;
		}

		++current;
	}

	eor = current - 1; // remove dummy char

	if (sor <= eor)
		eAppendItems(analysis, sor, eor, control, dir);

	return hasBidi;
}

void Renderer::applyBlockProperties(const AbstractBlock *block) {
	eSetFont(block);
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
				_currentPen = &_palette->linkFg->p;
				_currentPenSelected = &_palette->selectLinkFg->p;
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
			_currentPen = &_palette->linkFg->p;
			_currentPenSelected = &_palette->selectLinkFg->p;
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
