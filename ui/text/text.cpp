// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text.h"

#include "ui/text/text_isolated_emoji.h"
#include "ui/text/text_parser.h"
#include "ui/text/text_renderer.h"
#include "ui/text/text_spoiler_data.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_basic.h"

namespace Ui::Text {
namespace {

constexpr auto kDefaultSpoilerCacheCapacity = 24;

[[nodiscard]] Qt::LayoutDirection StringDirection(
		const QString &str,
		int from,
		int to) {
	auto p = reinterpret_cast<const ushort*>(str.unicode()) + from;
	const auto end = p + (to - from);
	while (p < end) {
		uint ucs4 = *p;
		if (QChar::isHighSurrogate(ucs4) && p < end - 1) {
			ushort low = p[1];
			if (QChar::isLowSurrogate(low)) {
				ucs4 = QChar::surrogateToUcs4(ucs4, low);
				++p;
			}
		}
		switch (QChar::direction(ucs4)) {
		case QChar::DirL:
			return Qt::LeftToRight;
		case QChar::DirR:
		case QChar::DirAL:
			return Qt::RightToLeft;
		default:
			break;
		}
		++p;
	}
	return Qt::LayoutDirectionAuto;
}

bool IsParagraphSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::LineFeed:
		return true;
	default:
		break;
	}
	return false;
}

} // namespace
} // namespace Ui::Text

const TextParseOptions kDefaultTextOptions = {
	TextParseLinks | TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions kMarkupTextOptions = {
	TextParseLinks | TextParseMultiline | TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions kPlainTextOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

namespace Ui::Text {

SpoilerMessCache::SpoilerMessCache(int capacity) : _capacity(capacity) {
	Expects(capacity > 0);

	_cache.reserve(capacity);
}

not_null<SpoilerMessCached*> SpoilerMessCache::lookup(QColor color) {
	for (auto &entry : _cache) {
		if (entry.color == color) {
			return &entry.mess;
		}
	}
	Assert(_cache.size() < _capacity);
	_cache.push_back({
		.mess = Ui::SpoilerMessCached(DefaultTextSpoilerMask(), color),
		.color = color,
	});
	return &_cache.back().mess;
}

void SpoilerMessCache::reset() {
	_cache.clear();
}

not_null<SpoilerMessCache*> DefaultSpoilerCache() {
	struct Data {
		Data() : cache(kDefaultSpoilerCacheCapacity) {
			style::PaletteChanged() | rpl::start_with_next([=] {
				cache.reset();
			}, lifetime);
		}

		SpoilerMessCache cache;
		rpl::lifetime lifetime;
	};

	static auto data = Data();
	return &data.cache;
}

String::String(int32 minResizeWidth)
: _minResizeWidth(minResizeWidth) {
}

String::String(
	const style::TextStyle &st,
	const QString &text,
	const TextParseOptions &options,
	int32 minResizeWidth)
: _minResizeWidth(minResizeWidth) {
	setText(st, text, options);
}

String::String(String &&other) = default;

String &String::operator=(String &&other) = default;

String::~String() = default;

void String::setText(const style::TextStyle &st, const QString &text, const TextParseOptions &options) {
	_st = &st;
	clear();
	{
		Parser parser(this, { text }, options, {});
	}
	recountNaturalSize(true, options.dir);
}

void String::recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir) {
	NewlineBlock *lastNewline = 0;

	_maxWidth = _minHeight = 0;
	int32 lineHeight = 0;
	int32 lastNewlineStart = 0;
	QFixed _width = 0, last_rBearing = 0, last_rPadding = 0;
	for (auto &block : _blocks) {
		auto b = block.get();
		auto _btype = b->type();
		auto blockHeight = CountBlockHeight(b, _st);
		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			if (initial) {
				Qt::LayoutDirection dir = optionsDir;
				if (dir == Qt::LayoutDirectionAuto) {
					dir = StringDirection(_text, lastNewlineStart, b->from());
				}
				if (lastNewline) {
					lastNewline->_nextDir = dir;
				} else {
					_startDir = dir;
				}
			}
			lastNewlineStart = b->from();
			lastNewline = &block.unsafe<NewlineBlock>();

			_minHeight += lineHeight;
			lineHeight = 0;
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();

			accumulate_max(_maxWidth, _width);
			_width = (b->f_width() - last_rBearing);
			continue;
		}

		auto b__f_rbearing = b->f_rbearing(); // cache

		// We need to accumulate max width after each block, because
		// some blocks have width less than -1 * previous right bearing.
		// In that cases the _width gets _smaller_ after moving to the next block.
		//
		// But when we layout block and we're sure that _maxWidth is enough
		// for all the blocks to fit on their line we check each block, even the
		// intermediate one with a large negative right bearing.
		accumulate_max(_maxWidth, _width);

		_width += last_rBearing + (last_rPadding + b->f_width() - b__f_rbearing);
		lineHeight = qMax(lineHeight, blockHeight);

		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		continue;
	}
	if (initial) {
		Qt::LayoutDirection dir = optionsDir;
		if (dir == Qt::LayoutDirectionAuto) {
			dir = StringDirection(_text, lastNewlineStart, _text.size());
		}
		if (lastNewline) {
			lastNewline->_nextDir = dir;
		} else {
			_startDir = dir;
		}
	}
	if (_width > 0) {
		if (!lineHeight) lineHeight = CountBlockHeight(_blocks.back().get(), _st);
		_minHeight += lineHeight;
		accumulate_max(_maxWidth, _width);
	}
}

int String::countMaxMonospaceWidth() const {
	auto result = QFixed();
	auto paragraphWidth = QFixed();
	auto fullMonospace = true;
	QFixed _width = 0, last_rBearing = 0, last_rPadding = 0;
	for (auto &block : _blocks) {
		auto b = block.get();
		auto _btype = b->type();
		if (_btype == TextBlockTNewline) {
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();

			if (fullMonospace) {
				accumulate_max(paragraphWidth, _width);
				accumulate_max(result, paragraphWidth);
				paragraphWidth = 0;
			} else {
				fullMonospace = true;
			}
			_width = (b->f_width() - last_rBearing);
			continue;
		}
		if (!(b->flags() & (TextBlockFPre | TextBlockFCode))
			&& (b->type() != TextBlockTSkip)) {
			fullMonospace = false;
		}
		auto b__f_rbearing = b->f_rbearing(); // cache

		// We need to accumulate max width after each block, because
		// some blocks have width less than -1 * previous right bearing.
		// In that cases the _width gets _smaller_ after moving to the next block.
		//
		// But when we layout block and we're sure that _maxWidth is enough
		// for all the blocks to fit on their line we check each block, even the
		// intermediate one with a large negative right bearing.
		if (fullMonospace) {
			accumulate_max(paragraphWidth, _width);
		}
		_width += last_rBearing + (last_rPadding + b->f_width() - b__f_rbearing);

		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		continue;
	}
	if (_width > 0 && fullMonospace) {
		accumulate_max(paragraphWidth, _width);
		accumulate_max(result, paragraphWidth);
	}
	return result.ceil().toInt();
}

void String::setMarkedText(const style::TextStyle &st, const TextWithEntities &textWithEntities, const TextParseOptions &options, const std::any &context) {
	_st = &st;
	clear();
	{
		// utf codes of the text display for emoji extraction
//		auto text = textWithEntities.text;
//		auto newText = QString();
//		newText.reserve(8 * text.size());
//		newText.append("\t{ ");
//		for (const QChar *ch = text.constData(), *e = ch + text.size(); ch != e; ++ch) {
//			if (*ch == TextCommand) {
//				break;
//			} else if (IsNewline(*ch)) {
//				newText.append("},").append(*ch).append("\t{ ");
//			} else {
//				if (ch->isHighSurrogate() || ch->isLowSurrogate()) {
//					if (ch->isHighSurrogate() && (ch + 1 != e) && ((ch + 1)->isLowSurrogate())) {
//						newText.append("0x").append(QString::number((uint32(ch->unicode()) << 16) | uint32((ch + 1)->unicode()), 16).toUpper()).append("U, ");
//						++ch;
//					} else {
//						newText.append("BADx").append(QString::number(ch->unicode(), 16).toUpper()).append("U, ");
//					}
//				} else {
//					newText.append("0x").append(QString::number(ch->unicode(), 16).toUpper()).append("U, ");
//				}
//			}
//		}
//		newText.append("},\n\n").append(text);
//		Parser parser(this, { newText, EntitiesInText() }, options, context);

		Parser parser(this, textWithEntities, options, context);
	}
	recountNaturalSize(true, options.dir);
}

void String::setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk) {
	if (!lnkIndex || lnkIndex > _links.size()) return;
	_links[lnkIndex - 1] = lnk;
}

void String::setSpoilerRevealed(bool revealed, anim::type animated) {
	if (!_spoiler) {
		return;
	} else if (_spoiler->revealed == revealed) {
		if (animated == anim::type::instant
			&& _spoiler->revealAnimation.animating()) {
			_spoiler->revealAnimation.stop();
			_spoiler->animation.repaintCallback()();
		}
		return;
	}
	_spoiler->revealed = revealed;
	if (animated == anim::type::instant) {
		_spoiler->revealAnimation.stop();
		_spoiler->animation.repaintCallback()();
	} else {
		_spoiler->revealAnimation.start(
			_spoiler->animation.repaintCallback(),
			revealed ? 0. : 1.,
			revealed ? 1. : 0.,
			st::fadeWrapDuration);
	}
}

void String::setSpoilerLink(const ClickHandlerPtr &lnk) {
	_spoiler->link = lnk;
}

bool String::hasLinks() const {
	return !_links.isEmpty();
}

bool String::hasSpoilers() const {
	return (_spoiler != nullptr);
}

bool String::hasSkipBlock() const {
	return _blocks.empty() ? false : _blocks.back()->type() == TextBlockTSkip;
}

bool String::updateSkipBlock(int width, int height) {
	if (!_blocks.empty() && _blocks.back()->type() == TextBlockTSkip) {
		const auto block = static_cast<SkipBlock*>(_blocks.back().get());
		if (block->width() == width && block->height() == height) {
			return false;
		}
		_text.resize(block->from());
		_blocks.pop_back();
	}
	_text.push_back('_');
	_blocks.push_back(Block::Skip(
		_st->font,
		_text,
		_text.size() - 1,
		width,
		height,
		0,
		0));
	recountNaturalSize(false);
	return true;
}

bool String::removeSkipBlock() {
	if (_blocks.empty() || _blocks.back()->type() != TextBlockTSkip) {
		return false;
	}
	_text.resize(_blocks.back()->from());
	_blocks.pop_back();
	recountNaturalSize(false);
	return true;
}

int String::countWidth(int width, bool breakEverywhere) const {
	if (QFixed(width) >= _maxWidth) {
		return _maxWidth.ceil().toInt();
	}

	QFixed maxLineWidth = 0;
	enumerateLines(width, breakEverywhere, [&](QFixed lineWidth, int lineHeight) {
		if (lineWidth > maxLineWidth) {
			maxLineWidth = lineWidth;
		}
	});
	return maxLineWidth.ceil().toInt();
}

int String::countHeight(int width, bool breakEverywhere) const {
	if (QFixed(width) >= _maxWidth) {
		return _minHeight;
	}
	int result = 0;
	enumerateLines(width, breakEverywhere, [&](QFixed lineWidth, int lineHeight) {
		result += lineHeight;
	});
	return result;
}

void String::countLineWidths(int width, QVector<int> *lineWidths, bool breakEverywhere) const {
	enumerateLines(width, breakEverywhere, [&](QFixed lineWidth, int lineHeight) {
		lineWidths->push_back(lineWidth.ceil().toInt());
	});
}

template <typename Callback>
void String::enumerateLines(
		int w,
		bool breakEverywhere,
		Callback callback) const {
	QFixed width = w;
	if (width < _minResizeWidth) width = _minResizeWidth;

	int lineHeight = 0;
	QFixed widthLeft = width, last_rBearing = 0, last_rPadding = 0;
	bool longWordLine = true;
	for (auto &b : _blocks) {
		auto _btype = b->type();
		int blockHeight = CountBlockHeight(b.get(), _st);

		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			callback(width - widthLeft, lineHeight);

			lineHeight = 0;
			last_rBearing = b->f_rbearing();
			last_rPadding = b->f_rpadding();
			widthLeft = width - (b->f_width() - last_rBearing);

			longWordLine = true;
			continue;
		}
		auto b__f_rbearing = b->f_rbearing();
		auto newWidthLeft = widthLeft - last_rBearing - (last_rPadding + b->f_width() - b__f_rbearing);
		if (newWidthLeft >= 0) {
			last_rBearing = b__f_rbearing;
			last_rPadding = b->f_rpadding();
			widthLeft = newWidthLeft;

			lineHeight = qMax(lineHeight, blockHeight);

			longWordLine = false;
			continue;
		}

		if (_btype == TextBlockTText) {
			const auto t = &b.unsafe<TextBlock>();
			if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
				last_rPadding += b->f_rpadding();

				lineHeight = qMax(lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			auto f_wLeft = widthLeft;
			int f_lineHeight = lineHeight;
			for (auto j = t->_words.cbegin(), e = t->_words.cend(), f = j; j != e; ++j) {
				bool wordEndsHere = (j->f_width() >= 0);
				auto j_width = wordEndsHere ? j->f_width() : -j->f_width();

				auto newWidthLeft = widthLeft - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
				if (newWidthLeft >= 0) {
					last_rBearing = j->f_rbearing();
					last_rPadding = j->f_rpadding();
					widthLeft = newWidthLeft;

					lineHeight = qMax(lineHeight, blockHeight);

					if (wordEndsHere) {
						longWordLine = false;
					}
					if (wordEndsHere || longWordLine) {
						f_wLeft = widthLeft;
						f_lineHeight = lineHeight;
						f = j + 1;
					}
					continue;
				}

				if (f != j && !breakEverywhere) {
					j = f;
					widthLeft = f_wLeft;
					lineHeight = f_lineHeight;
					j_width = (j->f_width() >= 0) ? j->f_width() : -j->f_width();
				}

				callback(width - widthLeft, lineHeight);

				lineHeight = qMax(0, blockHeight);
				last_rBearing = j->f_rbearing();
				last_rPadding = j->f_rpadding();
				widthLeft = width - (j_width - last_rBearing);

				longWordLine = !wordEndsHere;
				f = j + 1;
				f_wLeft = widthLeft;
				f_lineHeight = lineHeight;
			}
			continue;
		}

		callback(width - widthLeft, lineHeight);

		lineHeight = qMax(0, blockHeight);
		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		widthLeft = width - (b->f_width() - last_rBearing);

		longWordLine = true;
		continue;
	}
	if (widthLeft < width) {
		callback(width - widthLeft, lineHeight);
	}
}

void String::draw(QPainter &p, const PaintContext &context) const {
	Renderer(*this).draw(p, context);
}

void String::draw(Painter &p, int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, TextSelection selection, bool fullWidthSelection) const {
//	p.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	Renderer(*this).draw(p, {
		.position = { left, top },
		.availableWidth = w,
		.align = align,
		.clip = (yTo >= 0
			? QRect(left, top + yFrom, w, yTo - yFrom)
			: QRect()),
		.palette = &p.textPalette(),
		.paused = p.inactive(),
		.selection = selection,
		.fullWidthSelection = fullWidthSelection,
	});
}

void String::drawElided(Painter &p, int32 left, int32 top, int32 w, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
//	p.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	Renderer(*this).draw(p, {
		.position = { left, top },
		.availableWidth = w,
		.align = align,
		.clip = (yTo >= 0
			? QRect(left, top + yFrom, w, yTo - yFrom)
			: QRect()),
		.palette = &p.textPalette(),
		.paused = p.inactive(),
		.selection = selection,
		.elisionLines = lines,
		.elisionRemoveFromEnd = removeFromEnd,
	});
}

void String::drawLeft(Painter &p, int32 left, int32 top, int32 width, int32 outerw, style::align align, int32 yFrom, int32 yTo, TextSelection selection) const {
	Renderer(*this).draw(p, {
		.position = { left, top },
		.availableWidth = width,
		.align = align,
		.clip = (yTo >= 0
			? QRect(left, top + yFrom, width, yTo - yFrom)
			: QRect()),
		.palette = &p.textPalette(),
		.paused = p.inactive(),
		.selection = selection,
	});
	draw(p, style::RightToLeft() ? (outerw - left - width) : left, top, width, align, yFrom, yTo, selection);
}

void String::drawLeftElided(Painter &p, int32 left, int32 top, int32 width, int32 outerw, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
	drawElided(p, style::RightToLeft() ? (outerw - left - width) : left, top, width, lines, align, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
}

void String::drawRight(Painter &p, int32 right, int32 top, int32 width, int32 outerw, style::align align, int32 yFrom, int32 yTo, TextSelection selection) const {
	drawLeft(p, (outerw - right - width), top, width, outerw, align, yFrom, yTo, selection);
}

void String::drawRightElided(Painter &p, int32 right, int32 top, int32 width, int32 outerw, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere, TextSelection selection) const {
	drawLeftElided(p, (outerw - right - width), top, width, outerw, lines, align, yFrom, yTo, removeFromEnd, breakEverywhere, selection);
}

StateResult String::getState(QPoint point, int width, StateRequest request) const {
	return Renderer(*this).getState(point, width, request);
}

StateResult String::getStateLeft(QPoint point, int width, int outerw, StateRequest request) const {
	return getState(style::rtlpoint(point, outerw), width, request);
}

StateResult String::getStateElided(QPoint point, int width, StateRequestElided request) const {
	return Renderer(*this).getStateElided(point, width, request);
}

StateResult String::getStateElidedLeft(QPoint point, int width, int outerw, StateRequestElided request) const {
	return getStateElided(style::rtlpoint(point, outerw), width, request);
}

TextSelection String::adjustSelection(TextSelection selection, TextSelectType selectType) const {
	uint16 from = selection.from, to = selection.to;
	if (from < _text.size() && from <= to) {
		if (to > _text.size()) to = _text.size();
		if (selectType == TextSelectType::Paragraphs) {

			// Full selection of monospace entity.
			for (const auto &b : _blocks) {
				if (b->from() < from) {
					continue;
				}
				if (!IsMono(b->flags())) {
					break;
				}
				const auto &entities = toTextWithEntities().entities;
				const auto eIt = ranges::find_if(entities, [&](
						const EntityInText &e) {
					return (e.type() == EntityType::Pre
							|| e.type() == EntityType::Code)
						&& (from >= e.offset())
						&& ((e.offset() + e.length()) >= to);
				});
				if (eIt != entities.end()) {
					from = eIt->offset();
					to = eIt->offset() + eIt->length();
					while (to > 0 && IsSpace(_text.at(to - 1))) {
						--to;
					}
					if (to >= from) {
						return { from, to };
					}
				}
				break;
			}

			if (!IsParagraphSeparator(_text.at(from))) {
				while (from > 0 && !IsParagraphSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (IsParagraphSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !IsParagraphSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		} else if (selectType == TextSelectType::Words) {
			if (!IsWordSeparator(_text.at(from))) {
				while (from > 0 && !IsWordSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (IsWordSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !IsWordSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		}
	}
	return { from, to };
}

bool String::isEmpty() const {
	return _blocks.empty() || _blocks[0]->type() == TextBlockTSkip;
}

uint16 String::countBlockEnd(const TextBlocks::const_iterator &i, const TextBlocks::const_iterator &e) const {
	return (i + 1 == e) ? _text.size() : (*(i + 1))->from();
}

uint16 String::countBlockLength(const String::TextBlocks::const_iterator &i, const String::TextBlocks::const_iterator &e) const {
	return countBlockEnd(i, e) - (*i)->from();
}

template <
	typename AppendPartCallback,
	typename ClickHandlerStartCallback,
	typename ClickHandlerFinishCallback,
	typename FlagsChangeCallback>
void String::enumerateText(
		TextSelection selection,
		AppendPartCallback appendPartCallback,
		ClickHandlerStartCallback clickHandlerStartCallback,
		ClickHandlerFinishCallback clickHandlerFinishCallback,
		FlagsChangeCallback flagsChangeCallback) const {
	if (isEmpty() || selection.empty()) {
		return;
	}

	int lnkIndex = 0;
	uint16 lnkFrom = 0;

	int spoilerIndex = 0;
	uint16 spoilerFrom = 0;

	int32 flags = 0;
	for (auto i = _blocks.cbegin(), e = _blocks.cend(); true; ++i) {
		int blockLnkIndex = (i == e) ? 0 : (*i)->lnkIndex();
		int blockSpoilerIndex = (i == e) ? 0 : (*i)->spoilerIndex();
		uint16 blockFrom = (i == e) ? _text.size() : (*i)->from();
		int32 blockFlags = (i == e) ? 0 : (*i)->flags();

		if (IsMono(blockFlags)) {
			blockLnkIndex = 0;
		}
		if (blockLnkIndex && !_links.at(blockLnkIndex - 1)) { // ignore empty links
			blockLnkIndex = 0;
		}
		if (blockLnkIndex != lnkIndex) {
			if (lnkIndex) {
				auto rangeFrom = qMax(selection.from, lnkFrom);
				auto rangeTo = qMin(selection.to, blockFrom);
				if (rangeTo > rangeFrom) { // handle click handler
					const auto r = base::StringViewMid(_text, rangeFrom, rangeTo - rangeFrom);
					// Ignore links that are partially copied.
					const auto handler = (lnkFrom != rangeFrom || blockFrom != rangeTo)
						? nullptr
						: _links.at(lnkIndex - 1);
					const auto type = handler
						? handler->getTextEntity().type
						: EntityType::Invalid;
					clickHandlerFinishCallback(r, handler, type);
				}
			}
			lnkIndex = blockLnkIndex;
			if (lnkIndex) {
				lnkFrom = blockFrom;
				const auto handler = _links.at(lnkIndex - 1);
				clickHandlerStartCallback(handler
					? handler->getTextEntity().type
					: EntityType::Invalid);
			}
		}
		if (blockSpoilerIndex != spoilerIndex) {
			if (spoilerIndex) {
				auto rangeFrom = qMax(selection.from, spoilerFrom);
				auto rangeTo = qMin(selection.to, blockFrom);
				if (rangeTo > rangeFrom) { // handle click handler
					const auto r = base::StringViewMid(_text, rangeFrom, rangeTo - rangeFrom);
					// Ignore links that are partially copied.
					const auto handler = (spoilerFrom != rangeFrom
						|| blockFrom != rangeTo
						|| !_spoiler)
						? nullptr
						: _spoiler->link;
					const auto type = EntityType::Spoiler;
					clickHandlerFinishCallback(r, handler, type);
				}
			}
			spoilerIndex = blockSpoilerIndex;
			if (spoilerIndex) {
				spoilerFrom = blockFrom;
				clickHandlerStartCallback(EntityType::Spoiler);
			}
		}

		const auto checkBlockFlags = (blockFrom >= selection.from)
			&& (blockFrom <= selection.to);
		if (checkBlockFlags && blockFlags != flags) {
			flagsChangeCallback(flags, blockFlags);
			flags = blockFlags;
		}
		if (i == e || (lnkIndex ? lnkFrom : blockFrom) >= selection.to) {
			break;
		}

		const auto blockType = (*i)->type();
		if (blockType == TextBlockTSkip) continue;

		auto rangeFrom = qMax(selection.from, blockFrom);
		auto rangeTo = qMin(
			selection.to,
			uint16(blockFrom + countBlockLength(i, e)));
		if (rangeTo > rangeFrom) {
			const auto customEmojiData = (blockType == TextBlockTCustomEmoji)
				? static_cast<const CustomEmojiBlock*>(i->get())->_custom->entityData()
				: QString();
			appendPartCallback(
				base::StringViewMid(_text, rangeFrom, rangeTo - rangeFrom),
				customEmojiData);
		}
	}
}

bool String::hasPersistentAnimation() const {
	return _hasCustomEmoji || _spoiler;
}

void String::unloadPersistentAnimation() {
	if (_hasCustomEmoji) {
		for (const auto &block : _blocks) {
			const auto raw = block.get();
			if (raw->type() == TextBlockTCustomEmoji) {
				static_cast<const CustomEmojiBlock*>(raw)->_custom->unload();
			}
		}
	}
}

bool String::isOnlyCustomEmoji() const {
	return _isOnlyCustomEmoji;
}

OnlyCustomEmoji String::toOnlyCustomEmoji() const {
	if (!_isOnlyCustomEmoji) {
		return {};
	}
	auto result = OnlyCustomEmoji();
	result.lines.emplace_back();
	for (const auto &block : _blocks) {
		const auto raw = block.get();
		if (raw->type() == TextBlockTCustomEmoji) {
			const auto custom = static_cast<const CustomEmojiBlock*>(raw);
			result.lines.back().push_back({
				.entityData = custom->_custom->entityData(),
			});
		} else if (raw->type() == TextBlockTNewline) {
			result.lines.emplace_back();
		}
	}
	return result;
}

QString String::toString(TextSelection selection) const {
	return toText(selection, false, false).rich.text;
}

TextWithEntities String::toTextWithEntities(TextSelection selection) const {
	return toText(selection, false, true).rich;
}

TextForMimeData String::toTextForMimeData(TextSelection selection) const {
	return toText(selection, true, true);
}

TextForMimeData String::toText(
		TextSelection selection,
		bool composeExpanded,
		bool composeEntities) const {
	struct MarkdownTagTracker {
		TextBlockFlags flag = TextBlockFlags();
		EntityType type = EntityType();
		int start = 0;
	};
	auto result = TextForMimeData();
	result.rich.text.reserve(_text.size());
	if (composeExpanded) {
		result.expanded.reserve(_text.size());
	}
	const auto insertEntity = [&](EntityInText &&entity) {
		auto i = result.rich.entities.end();
		while (i != result.rich.entities.begin()) {
			auto j = i;
			if ((--j)->offset() <= entity.offset()) {
				break;
			}
			i = j;
		}
		result.rich.entities.insert(i, std::move(entity));
	};
	auto linkStart = 0;
	auto spoilerStart = 0;
	auto markdownTrackers = composeEntities
		? std::vector<MarkdownTagTracker>{
			{ TextBlockFItalic, EntityType::Italic },
			{ TextBlockFBold, EntityType::Bold },
			{ TextBlockFSemibold, EntityType::Semibold },
			{ TextBlockFUnderline, EntityType::Underline },
			{ TextBlockFPlainLink, EntityType::PlainLink },
			{ TextBlockFStrikeOut, EntityType::StrikeOut },
			{ TextBlockFCode, EntityType::Code }, // #TODO entities
			{ TextBlockFPre, EntityType::Pre },
		} : std::vector<MarkdownTagTracker>();
	const auto flagsChangeCallback = [&](int32 oldFlags, int32 newFlags) {
		if (!composeEntities) {
			return;
		}
		for (auto &tracker : markdownTrackers) {
			const auto flag = tracker.flag;
			if ((oldFlags & flag) && !(newFlags & flag)) {
				insertEntity({
					tracker.type,
					tracker.start,
					int(result.rich.text.size()) - tracker.start });
			} else if ((newFlags & flag) && !(oldFlags & flag)) {
				tracker.start = result.rich.text.size();
			}
		}
	};
	const auto clickHandlerStartCallback = [&](EntityType type) {
		if (type == EntityType::Spoiler) {
			spoilerStart = result.rich.text.size();
		} else {
			linkStart = result.rich.text.size();
		}
	};
	const auto clickHandlerFinishCallback = [&](
			QStringView inText,
			const ClickHandlerPtr &handler,
			EntityType type) {
		if (type == EntityType::Spoiler) {
			insertEntity({
				type,
				spoilerStart,
				int(result.rich.text.size() - spoilerStart),
				QString(),
			});
			return;
		}
		if (!handler || (!composeExpanded && !composeEntities)) {
			return;
		}
		// This logic is duplicated in TextForMimeData::WithExpandedLinks.
		const auto entity = handler->getTextEntity();
		const auto plainUrl = (entity.type == EntityType::Url)
			|| (entity.type == EntityType::Email);
		const auto full = plainUrl
			? QStringView(entity.data).mid(0, entity.data.size())
			: inText;
		const auto customTextLink = (entity.type == EntityType::CustomUrl);
		const auto internalLink = customTextLink
			&& entity.data.startsWith(qstr("internal:"));
		if (composeExpanded) {
			const auto sameAsTextLink = customTextLink
				&& (entity.data
					== UrlClickHandler::EncodeForOpening(full.toString()));
			if (customTextLink && !internalLink && !sameAsTextLink) {
				const auto &url = entity.data;
				result.expanded.append(qstr(" (")).append(url).append(')');
			}
		}
		if (composeEntities && !internalLink) {
			insertEntity({
				entity.type,
				linkStart,
				int(result.rich.text.size() - linkStart),
				plainUrl ? QString() : entity.data });
		}
	};
	const auto appendPartCallback = [&](
			QStringView part,
			const QString &customEmojiData) {
		result.rich.text += part;
		if (composeExpanded) {
			result.expanded += part;
		}
		if (composeEntities && !customEmojiData.isEmpty()) {
			insertEntity({
				EntityType::CustomEmoji,
				int(result.rich.text.size() - part.size()),
				int(part.size()),
				customEmojiData,
			});
		}
	};

	enumerateText(
		selection,
		appendPartCallback,
		clickHandlerStartCallback,
		clickHandlerFinishCallback,
		flagsChangeCallback);

	if (composeEntities) {
		const auto proj = [](const EntityInText &entity) {
			const auto type = entity.type();
			const auto isUrl = (type == EntityType::Url)
				|| (type == EntityType::CustomUrl)
				|| (type == EntityType::BotCommand)
				|| (type == EntityType::Mention)
				|| (type == EntityType::MentionName)
				|| (type == EntityType::Hashtag)
				|| (type == EntityType::Cashtag);
			return std::pair{ entity.offset(), isUrl ? 0 : 1 };
		};
		const auto pred = [&](const EntityInText &a, const EntityInText &b) {
			return proj(a) < proj(b);
		};
		std::sort(
			result.rich.entities.begin(),
			result.rich.entities.end(),
			pred);
	}

	return result;
}

bool String::isIsolatedEmoji() const {
	return _isIsolatedEmoji;
}

IsolatedEmoji String::toIsolatedEmoji() const {
	if (!_isIsolatedEmoji) {
		return {};
	}
	auto result = IsolatedEmoji();
	const auto skip = (_blocks.empty()
		|| _blocks.back()->type() != TextBlockTSkip) ? 0 : 1;
	if ((_blocks.size() > kIsolatedEmojiLimit + skip) || _spoiler) {
		return {};
	}
	auto index = 0;
	for (const auto &block : _blocks) {
		const auto type = block->type();
		if (block->lnkIndex()) {
			return {};
		} else if (type == TextBlockTEmoji) {
			result.items[index++] = block.unsafe<EmojiBlock>()._emoji;
		} else if (type == TextBlockTCustomEmoji) {
			result.items[index++]
				= block.unsafe<CustomEmojiBlock>()._custom->entityData();
		} else if (type != TextBlockTSkip) {
			return {};
		}
	}
	return result;
}

void String::clear() {
	clearFields();
	_text.clear();
}

void String::clearFields() {
	_blocks.clear();
	_links.clear();
	_spoiler = nullptr;
	_maxWidth = _minHeight = 0;
	_startDir = Qt::LayoutDirectionAuto;
}

bool IsBad(QChar ch) {
	return (ch == 0)
		|| (ch >= 8232 && ch < 8237)
		|| (ch >= 65024 && ch < 65040 && ch != 65039)
		|| (ch >= 127 && ch < 160 && ch != 156)

		// qt harfbuzz crash see https://github.com/telegramdesktop/tdesktop/issues/4551
		|| (Platform::IsMac() && ch == 6158);
}

bool IsWordSeparator(QChar ch) {
	switch (ch.unicode()) {
	case QChar::Space:
	case QChar::LineFeed:
	case '.':
	case ',':
	case '?':
	case '!':
	case '@':
	case '#':
	case '$':
	case ':':
	case ';':
	case '-':
	case '<':
	case '>':
	case '[':
	case ']':
	case '(':
	case ')':
	case '{':
	case '}':
	case '=':
	case '/':
	case '+':
	case '%':
	case '&':
	case '^':
	case '*':
	case '\'':
	case '"':
	case '`':
	case '~':
	case '|':
		return true;
	default:
		break;
	}
	return false;
}

bool IsAlmostLinkEnd(QChar ch) {
	switch (ch.unicode()) {
	case '?':
	case ',':
	case '.':
	case '"':
	case ':':
	case '!':
	case '\'':
		return true;
	default:
		break;
	}
	return false;
}

bool IsLinkEnd(QChar ch) {
	return IsBad(ch)
		|| IsSpace(ch)
		|| IsNewline(ch)
		|| ch.isLowSurrogate()
		|| ch.isHighSurrogate();
}

bool IsNewline(QChar ch) {
	return (ch == QChar::LineFeed)
		|| (ch == 156);
}

bool IsSpace(QChar ch) {
	return ch.isSpace()
		|| (ch < 32)
		|| (ch == QChar::ParagraphSeparator)
		|| (ch == QChar::LineSeparator)
		|| (ch == QChar::ObjectReplacementCharacter)
		|| (ch == QChar::CarriageReturn)
		|| (ch == QChar::Tabulation)
		|| (ch == QChar(8203)/*Zero width space.*/);
}

bool IsDiac(QChar ch) { // diac and variation selectors
	return (ch.category() == QChar::Mark_NonSpacing)
		|| (ch == 1652)
		|| (ch >= 64606 && ch <= 64611);
}

bool IsReplacedBySpace(QChar ch) {
	// \xe2\x80[\xa8 - \xac\xad] // 8232 - 8237
	// QString from1 = QString::fromUtf8("\xe2\x80\xa8"), to1 = QString::fromUtf8("\xe2\x80\xad");
	// \xcc[\xb3\xbf\x8a] // 819, 831, 778
	// QString bad1 = QString::fromUtf8("\xcc\xb3"), bad2 = QString::fromUtf8("\xcc\xbf"), bad3 = QString::fromUtf8("\xcc\x8a");
	// [\x00\x01\x02\x07\x08\x0b-\x1f] // '\t' = 0x09
	return (/*code >= 0x00 && */ch <= 0x02)
		|| (ch >= 0x07 && ch <= 0x09)
		|| (ch >= 0x0b && ch <= 0x1f)
		|| (ch == 819)
		|| (ch == 831)
		|| (ch == 778)
		|| (ch >= 8232 && ch <= 8237);
}

bool IsTrimmed(QChar ch) {
	return (IsSpace(ch) || IsBad(ch));
}

} // namespace Ui::Text
