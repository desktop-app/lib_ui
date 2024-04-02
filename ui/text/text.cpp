// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text.h"

#include "ui/effects/spoiler_mess.h"
#include "ui/text/text_extended_data.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/text_parser.h"
#include "ui/text/text_renderer.h"
#include "ui/basic_click_handlers.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_basic.h"

namespace Ui {

const QString kQEllipsis = u"..."_q;

} // namespace Ui

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

struct SpoilerMessCache::Entry {
	SpoilerMessCached mess;
	QColor color;
};

SpoilerMessCache::SpoilerMessCache(int capacity) : _capacity(capacity) {
	Expects(capacity > 0);

	_cache.reserve(capacity);
}

SpoilerMessCache::~SpoilerMessCache() = default;

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

GeometryDescriptor SimpleGeometry(
		int availableWidth,
		int elisionLines,
		int elisionRemoveFromEnd,
		bool elisionBreakEverywhere) {
	constexpr auto wrap = [](
			Fn<LineGeometry(int line)> layout,
			bool breakEverywhere = false) {
		return GeometryDescriptor{ std::move(layout), breakEverywhere };
	};

	// Try to minimize captured values (to minimize Fn allocations).
	if (!elisionLines) {
		return wrap([=](int line) {
			return LineGeometry{ .width = availableWidth };
		});
	} else if (!elisionRemoveFromEnd) {
		return wrap([=](int line) {
			return LineGeometry{
				.width = availableWidth,
				.elided = (line + 1 >= elisionLines),
			};
		}, elisionBreakEverywhere);
	} else {
		return wrap([=](int line) {
			const auto elided = (line + 1 >= elisionLines);
			const auto removeFromEnd = (elided ? elisionRemoveFromEnd : 0);
			return LineGeometry{
				.width = availableWidth - removeFromEnd,
				.elided = elided,
			};
		}, elisionBreakEverywhere);
	}
};

void ValidateQuotePaintCache(
		QuotePaintCache &cache,
		const style::QuoteStyle &st) {
	const auto icon = st.icon.empty() ? nullptr : &st.icon;
	if (!cache.corners.isNull()
		&& cache.bgCached == cache.bg
		&& cache.outlines == cache.outlines
		&& (!st.header || cache.headerCached == cache.header)
		&& (!icon || cache.iconCached == cache.icon)) {
		return;
	}
	cache.bgCached = cache.bg;
	cache.outlinesCached = cache.outlines;
	if (st.header) {
		cache.headerCached = cache.header;
	}
	if (!st.icon.empty()) {
		cache.iconCached = cache.icon;
	}
	const auto radius = st.radius;
	const auto header = st.header;
	const auto outline = st.outline;
	const auto wiconsize = icon
		? (icon->width() + st.iconPosition.x())
		: 0;
	const auto hiconsize = icon
		? (icon->height() + st.iconPosition.y())
		: 0;
	const auto wcorner = std::max({ radius, outline, wiconsize });
	const auto hcorner = std::max({ header, radius, hiconsize });
	const auto middle = st::lineWidth;
	const auto wside = 2 * wcorner + middle;
	const auto hside = 2 * hcorner + middle;
	const auto full = QSize(wside, hside);
	const auto ratio = style::DevicePixelRatio();

	if (!cache.outlines[1].alpha()) {
		cache.outline = QImage();
	} else if (const auto outline = st.outline) {
		const auto third = (cache.outlines[2].alpha() != 0);
		const auto size = QSize(outline, outline * (third ? 6 : 4));
		cache.outline = QImage(
			size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		cache.outline.fill(cache.outlines[0]);
		cache.outline.setDevicePixelRatio(ratio);
		auto p = QPainter(&cache.outline);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();
		path.moveTo(outline, outline);
		path.lineTo(outline, outline * (third ? 4 : 3));
		path.lineTo(0, outline * (third ? 5 : 4));
		path.lineTo(0, outline * 2);
		path.lineTo(outline, outline);
		p.fillPath(path, cache.outlines[third ? 2 : 1]);
		if (third) {
			auto path = QPainterPath();
			path.moveTo(outline, outline * 3);
			path.lineTo(outline, outline * 5);
			path.lineTo(0, outline * 6);
			path.lineTo(0, outline * 4);
			path.lineTo(outline, outline * 3);
			p.fillPath(path, cache.outlines[1]);
		}
	}

	auto image = QImage(full * ratio, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(ratio);
	auto p = QPainter(&image);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);

	if (header) {
		p.setBrush(cache.header);
		p.setClipRect(outline, 0, wside - outline, header);
		p.drawRoundedRect(0, 0, wside, hcorner + radius, radius, radius);
	}
	if (outline) {
		const auto rect = QRect(0, 0, outline + radius * 2, hside);
		if (!cache.outline.isNull()) {
			const auto shift = QPoint(0, st.outlineShift);
			p.translate(shift);
			p.setBrush(cache.outline);
			p.setClipRect(QRect(-shift, QSize(outline, hside)));
			p.drawRoundedRect(rect.translated(-shift), radius, radius);
			p.translate(-shift);
		} else {
			p.setBrush(cache.outlines[0]);
			p.setClipRect(0, 0, outline, hside);
			p.drawRoundedRect(rect, radius, radius);
		}
	}
	p.setBrush(cache.bg);
	p.setClipRect(outline, header, wside - outline, hside - header);
	p.drawRoundedRect(0, 0, wside, hside, radius, radius);
	if (icon) {
		p.setClipping(false);
		const auto left = wside - icon->width() - st.iconPosition.x();
		const auto top = st.iconPosition.y();
		icon->paint(p, left, top, wside, cache.icon);
	}

	p.end();
	cache.corners = std::move(image);
}

void FillQuotePaint(
		QPainter &p,
		QRect rect,
		const QuotePaintCache &cache,
		const style::QuoteStyle &st,
		SkipBlockPaintParts parts) {
	const auto &image = cache.corners;
	const auto ratio = int(image.devicePixelRatio());
	const auto iwidth = image.width() / ratio;
	const auto iheight = image.height() / ratio;
	const auto imiddle = st::lineWidth;
	const auto whalf = (iwidth - imiddle) / 2;
	const auto hhalf = (iheight - imiddle) / 2;
	const auto x = rect.left();
	const auto width = rect.width();
	auto y = rect.top();
	auto height = rect.height();
	if (!parts.skippedTop) {
		const auto top = std::min(height, hhalf);
		p.drawImage(
			QRect(x, y, whalf, top),
			image,
			QRect(0, 0, whalf * ratio, top * ratio));
		p.drawImage(
			QRect(x + width - whalf, y, whalf, top),
			image,
			QRect((iwidth - whalf) * ratio, 0, whalf * ratio, top * ratio));
		if (const auto middle = width - 2 * whalf) {
			const auto header = st.header;
			const auto fillHeader = std::min(header, top);
			if (fillHeader) {
				p.fillRect(x + whalf, y, middle, fillHeader, cache.header);
			}
			if (const auto fillBody = top - fillHeader) {
				p.fillRect(
					QRect(x + whalf, y + fillHeader, middle, fillBody),
					cache.bg);
			}
		}
		height -= top;
		if (!height) {
			return;
		}
		y += top;
		rect.setTop(y);
	}
	const auto outline = st.outline;
	if (!parts.skipBottom) {
		const auto bottom = std::min(height, hhalf);
		const auto skip = !cache.outline.isNull() ? outline : 0;
		p.drawImage(
			QRect(x + skip, y + height - bottom, whalf - skip, bottom),
			image,
			QRect(
				skip * ratio,
				(iheight - bottom) * ratio,
				(whalf - skip) * ratio,
				bottom * ratio));
		p.drawImage(
			QRect(
				x + width - whalf,
				y + height - bottom,
				whalf,
				bottom),
			image,
			QRect(
				(iwidth - whalf) * ratio,
				(iheight - bottom) * ratio,
				whalf * ratio,
				bottom * ratio));
		if (const auto middle = width - 2 * whalf) {
			p.fillRect(
				QRect(x + whalf, y + height - bottom, middle, bottom),
				cache.bg);
		}
		if (skip) {
			if (cache.bottomCorner.size() != QSize(skip, whalf)) {
				cache.bottomCorner = QImage(
					QSize(skip, hhalf) * ratio,
					QImage::Format_ARGB32_Premultiplied);
				cache.bottomCorner.setDevicePixelRatio(ratio);
				cache.bottomCorner.fill(Qt::transparent);

				cache.bottomRounding = QImage(
					QSize(skip, hhalf) * ratio,
					QImage::Format_ARGB32_Premultiplied);
				cache.bottomRounding.setDevicePixelRatio(ratio);
				cache.bottomRounding.fill(Qt::transparent);
				const auto radius = st.radius;
				auto q = QPainter(&cache.bottomRounding);
				auto hq = PainterHighQualityEnabler(q);
				q.setPen(Qt::NoPen);
				q.setBrush(Qt::white);
				q.drawRoundedRect(
					0,
					-2 * radius,
					skip + 2 * radius,
					hhalf + 2 * radius,
					radius,
					radius);
			}
			auto q = QPainter(&cache.bottomCorner);
			const auto skipped = (height - bottom)
				+ (parts.skippedTop ? int(parts.skippedTop) : hhalf)
				- st.outlineShift;
			q.translate(0, -skipped);
			q.fillRect(0, skipped, skip, bottom, cache.outline);
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawImage(0, skipped + bottom - hhalf, cache.bottomRounding);
			q.end();

			p.drawImage(
				QRect(x, y + height - bottom, skip, bottom),
				cache.bottomCorner,
				QRect(0, 0, skip * ratio, bottom * ratio));
		}
		height -= bottom;
		if (!height) {
			return;
		}
		rect.setHeight(height);
	}
	if (outline) {
		if (!cache.outline.isNull()) {
			const auto skipped = st.outlineShift
				- (parts.skippedTop ? int(parts.skippedTop) : hhalf);
			const auto top = y + skipped;
			p.translate(x, top);
			p.fillRect(0, -skipped, outline, height, cache.outline);
			p.translate(-x, -top);
		} else {
			p.fillRect(x, y, outline, height, cache.outlines[0]);
		}
	}
	p.fillRect(x + outline, y, width - outline, height, cache.bg);
}

String::ExtendedWrap::ExtendedWrap() noexcept = default;

String::ExtendedWrap::ExtendedWrap(ExtendedWrap &&other) noexcept
: unique_ptr(std::move(other)) {
	adjustFrom(&other);
}

String::ExtendedWrap &String::ExtendedWrap::operator=(
		ExtendedWrap &&other) noexcept {
	*static_cast<unique_ptr*>(this) = std::move(other);
	adjustFrom(&other);
	return *this;
}

String::ExtendedWrap::ExtendedWrap(
	std::unique_ptr<ExtendedData> &&other) noexcept
: unique_ptr(std::move(other)) {
	Assert(!get() || !get()->spoiler);
}

String::ExtendedWrap &String::ExtendedWrap::operator=(
		std::unique_ptr<ExtendedData> &&other) noexcept {
	*static_cast<unique_ptr*>(this) = std::move(other);
	Assert(!get() || !get()->spoiler);
	return *this;
}

String::ExtendedWrap::~ExtendedWrap() = default;

void String::ExtendedWrap::adjustFrom(const ExtendedWrap *other) {
	const auto data = get();
	const auto raw = [](auto pointer) {
		return reinterpret_cast<quintptr>(pointer);
	};
	const auto adjust = [&](auto &link) {
		const auto otherText = raw(link->text().get());
		link->setText(
			reinterpret_cast<String*>(otherText + raw(this) - raw(other)));
	};
	if (data) {
		if (const auto spoiler = data->spoiler.get()) {
			if (spoiler->link) {
				adjust(spoiler->link);
			}
		}
		for (auto &quote : data->quotes) {
			if (quote.copy) {
				adjust(quote.copy);
			}
		}
	}
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

String::String(
	const style::TextStyle &st,
	const TextWithEntities &textWithEntities,
	const TextParseOptions &options,
	int32 minResizeWidth,
	const std::any &context)
: _minResizeWidth(minResizeWidth) {
	setMarkedText(st, textWithEntities, options, context);
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

void String::recountNaturalSize(
		bool initial,
		Qt::LayoutDirection optionsDirection) {
	auto lastNewline = (NewlineBlock*)nullptr;
	auto lastNewlineStart = 0;
	const auto computeParagraphDirection = [&](int paragraphEnd) {
		const auto direction = (optionsDirection != Qt::LayoutDirectionAuto)
			? optionsDirection
			: StringDirection(_text, lastNewlineStart, paragraphEnd);
		if (lastNewline) {
			lastNewline->_paragraphLTR = (direction == Qt::LeftToRight);
			lastNewline->_paragraphRTL = (direction == Qt::RightToLeft);
		} else {
			_startParagraphLTR = (direction == Qt::LeftToRight);
			_startParagraphRTL = (direction == Qt::RightToLeft);
		}
	};

	auto qindex = quoteIndex(nullptr);
	auto quote = quoteByIndex(qindex);
	auto qpadding = quotePadding(quote);
	auto qminwidth = quoteMinWidth(quote);
	auto qmaxwidth = QFixed(qminwidth);
	auto qoldheight = 0;

	_maxWidth = 0;
	_minHeight = qpadding.top();
	auto lineHeight = 0;
	auto maxWidth = QFixed();
	auto width = QFixed(qminwidth);
	auto last_rBearing = QFixed();
	auto last_rPadding = QFixed();
	for (auto &block : _blocks) {
		const auto b = block.get();
		const auto _btype = b->type();
		const auto blockHeight = CountBlockHeight(b, _st);
		if (_btype == TextBlockType::Newline) {
			if (!lineHeight) {
				lineHeight = blockHeight;
			}
			accumulate_max(maxWidth, width);
			accumulate_max(qmaxwidth, width);

			const auto index = quoteIndex(b);
			if (qindex != index) {
				_minHeight += qpadding.bottom();
				if (quote) {
					quote->maxWidth = qmaxwidth.ceil().toInt();
					quote->minHeight = _minHeight - qoldheight;
				}
				qoldheight = _minHeight;
				qindex = index;
				quote = quoteByIndex(qindex);
				qpadding = quotePadding(quote);
				qminwidth = quoteMinWidth(quote);
				qmaxwidth = qminwidth;
				_minHeight += qpadding.top();
				qpadding.setTop(0);
			}
			if (initial) {
				computeParagraphDirection(b->position());
			}
			lastNewlineStart = b->position();
			lastNewline = &block.unsafe<NewlineBlock>();

			_minHeight += lineHeight;
			lineHeight = 0;
			last_rBearing = 0;// b->f_rbearing(); (0 for newline)
			last_rPadding = 0;// b->f_rpadding(); (0 for newline)

			width = qminwidth;
			// + (b->f_width() - last_rBearing); (0 for newline)
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
		accumulate_max(maxWidth, width);
		accumulate_max(qmaxwidth, width);

		width += last_rBearing + (last_rPadding + b->f_width() - b__f_rbearing);
		lineHeight = qMax(lineHeight, blockHeight);

		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		continue;
	}
	if (initial) {
		computeParagraphDirection(_text.size());
	}
	if (width > 0) {
		if (!lineHeight) {
			lineHeight = CountBlockHeight(_blocks.back().get(), _st);
		}
		_minHeight += qpadding.top() + lineHeight + qpadding.bottom();
		accumulate_max(maxWidth, width);
		accumulate_max(qmaxwidth, width);
	}
	_maxWidth = maxWidth.ceil().toInt();
	if (quote) {
		quote->maxWidth = qmaxwidth.ceil().toInt();
		quote->minHeight = _minHeight - qoldheight;
		_endsWithQuote = true;
	} else {
		_endsWithQuote = false;
	}
}

int String::countMaxMonospaceWidth() const {
	auto result = 0;
	if (_extended) {
		for (const auto &quote : _extended->quotes) {
			if (quote.pre) {
				accumulate_max(result, quote.maxWidth);
			}
		}
	}
	return result;
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

void String::setLink(uint16 index, const ClickHandlerPtr &link) {
	const auto extended = _extended.get();
	if (extended && index > 0 && index <= extended->links.size()) {
		extended->links[index - 1] = link;
	}
}

void String::setSpoilerRevealed(bool revealed, anim::type animated) {
	const auto data = _extended ? _extended->spoiler.get() : nullptr;
	if (!data) {
		return;
	} else if (data->revealed == revealed) {
		if (animated == anim::type::instant
			&& data->revealAnimation.animating()) {
			data->revealAnimation.stop();
			data->animation.repaintCallback()();
		}
		return;
	}
	data->revealed = revealed;
	if (animated == anim::type::instant) {
		data->revealAnimation.stop();
		data->animation.repaintCallback()();
	} else {
		data->revealAnimation.start(
			data->animation.repaintCallback(),
			revealed ? 0. : 1.,
			revealed ? 1. : 0.,
			st::fadeWrapDuration);
	}
}

void String::setSpoilerLinkFilter(Fn<bool(const ClickContext&)> filter) {
	Expects(_extended && _extended->spoiler);

	_extended->spoiler->link = std::make_shared<SpoilerClickHandler>(
		this,
		std::move(filter));
}

bool String::hasLinks() const {
	return _extended && !_extended->links.empty();
}

bool String::hasSpoilers() const {
	return _extended && (_extended->spoiler != nullptr);
}

bool String::hasSkipBlock() const {
	return !_blocks.empty()
		&& (_blocks.back()->type() == TextBlockType::Skip);
}

bool String::updateSkipBlock(int width, int height) {
	if (!_blocks.empty() && _blocks.back()->type() == TextBlockType::Skip) {
		const auto block = static_cast<SkipBlock*>(_blocks.back().get());
		if (block->f_width().toInt() == width && block->height() == height) {
			return false;
		}
		const auto size = block->position();
		_text.resize(size);
		_blocks.pop_back();
		removeModificationsAfter(size);
	} else if (_endsWithQuote) {
		insertModifications(_text.size(), 1);
		_text.push_back(QChar::LineFeed);
		_blocks.push_back(Block::Newline(
			_st->font,
			_text,
			_text.size() - 1,
			1,
			0,
			0,
			0));
		_skipBlockAddedNewline = true;
	}
	insertModifications(_text.size(), 1);
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
	if (_blocks.empty() || _blocks.back()->type() != TextBlockType::Skip) {
		return false;
	} else if (_skipBlockAddedNewline) {
		const auto size = _blocks.back()->position() - 1;
		_text.resize(size);
		_blocks.pop_back();
		_blocks.pop_back();
		_skipBlockAddedNewline = false;
		removeModificationsAfter(size);
	} else {
		const auto size = _blocks.back()->position();
		_text.resize(size);
		_blocks.pop_back();
		removeModificationsAfter(size);
	}
	recountNaturalSize(false);
	return true;
}

void String::insertModifications(int position, int delta) {
	auto &modifications = ensureExtended()->modifications;
	auto i = end(modifications);
	while (i != begin(modifications) && (i - 1)->position >= position) {
		--i;
		if (i->position < position) {
			break;
		} else if (delta > 0) {
			++i->position;
		} else if (i->position == position) {
			break;
		}
	}
	if (i != end(modifications) && i->position == position) {
		++i->skipped;
	} else {
		modifications.insert(i, {
			.position = position,
			.skipped = uint16(delta < 0 ? (-delta) : 0),
			.added = (delta > 0),
		});
	}
}

void String::removeModificationsAfter(int size) {
	if (!_extended) {
		return;
	}
	auto &modifications = _extended->modifications;
	for (auto i = end(modifications); i != begin(modifications);) {
		--i;
		if (i->position > size) {
			i = modifications.erase(i);
		} else if (i->position == size) {
			i->added = false;
			if (!i->skipped) {
				i = modifications.erase(i);
			}
		} else {
			break;
		}
	}
}

String::DimensionsResult String::countDimensions(
		GeometryDescriptor geometry) const {
	return countDimensions(std::move(geometry), {});
}

String::DimensionsResult String::countDimensions(
		GeometryDescriptor geometry,
		DimensionsRequest request) const {
	auto result = DimensionsResult();
	if (request.lineWidths && request.reserve) {
		result.lineWidths.reserve(request.reserve);
	}
	enumerateLines(geometry, [&](QFixed lineWidth, int lineBottom) {
		const auto width = lineWidth.ceil().toInt();
		if (request.lineWidths) {
			result.lineWidths.push_back(width);
		}
		result.width = std::max(result.width, width);
		result.height = lineBottom;
	});
	return result;

}

int String::countWidth(int width, bool breakEverywhere) const {
	if (QFixed(width) >= _maxWidth) {
		return _maxWidth;
	}

	QFixed maxLineWidth = 0;
	enumerateLines(width, breakEverywhere, [&](QFixed lineWidth, int) {
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
	enumerateLines(width, breakEverywhere, [&](auto, int lineBottom) {
		result = lineBottom;
	});
	return result;
}

std::vector<int> String::countLineWidths(int width) const {
	return countLineWidths(width, {});
}

std::vector<int> String::countLineWidths(
		int width,
		LineWidthsOptions options) const {
	auto result = std::vector<int>();
	if (options.reserve) {
		result.reserve(options.reserve);
	}
	enumerateLines(width, options.breakEverywhere, [&](QFixed lineWidth, int) {
		result.push_back(lineWidth.ceil().toInt());
	});
	return result;
}

template <typename Callback>
void String::enumerateLines(
		int w,
		bool breakEverywhere,
		Callback &&callback) const {
	if (isEmpty()) {
		return;
	}
	const auto width = std::max(w, _minResizeWidth);
	auto g = SimpleGeometry(width, 0, 0, false);
	g.breakEverywhere = breakEverywhere;
	enumerateLines(g, std::forward<Callback>(callback));
}

template <typename Callback>
void String::enumerateLines(
		GeometryDescriptor geometry,
		Callback &&callback) const {
	if (isEmpty()) {
		return;
	}

	const auto withElided = [&](bool elided) {
		if (geometry.outElided) {
			*geometry.outElided = elided;
		}
	};

	auto qindex = 0;
	auto quote = (QuoteDetails*)nullptr;
	auto qpadding = QMargins();

	auto top = 0;
	auto lineLeft = 0;
	auto lineWidth = 0;
	auto lineElided = false;
	auto widthLeft = QFixed(0);
	auto lineIndex = 0;
	const auto initNextLine = [&] {
		const auto line = geometry.layout(lineIndex++);
		lineLeft = line.left;
		lineWidth = line.width;
		lineElided = line.elided;
		if (quote && quote->maxWidth < lineWidth) {
			lineWidth = quote->maxWidth;
		}
		widthLeft = lineWidth - qpadding.left() - qpadding.right();
	};
	const auto initNextParagraph = [&](
			TextBlocks::const_iterator i,
			int16 paragraphIndex) {
		if (qindex != paragraphIndex) {
			top += qpadding.bottom();
			qindex = paragraphIndex;
			quote = quoteByIndex(qindex);
			qpadding = quotePadding(quote);
			top += qpadding.top();
			qpadding.setTop(0);
		}
		initNextLine();
	};

	if ((*_blocks.cbegin())->type() != TextBlockType::Newline) {
		initNextParagraph(_blocks.cbegin(), _startQuoteIndex);
	}

	auto lineHeight = 0;
	auto last_rBearing = QFixed();
	auto last_rPadding = QFixed();
	bool longWordLine = true;
	for (auto i = _blocks.cbegin(); i != _blocks.cend(); ++i) {
		const auto &b = *i;
		auto _btype = b->type();
		const auto blockHeight = CountBlockHeight(b.get(), _st);

		if (_btype == TextBlockType::Newline) {
			if (!lineHeight) {
				lineHeight = blockHeight;
			}
			const auto index = b.unsafe<const NewlineBlock>().quoteIndex();
			const auto changed = (qindex != index);
			if (changed) {
				lineHeight += qpadding.bottom();
			}

			callback(lineLeft + lineWidth - widthLeft, top += lineHeight);
			if (lineElided) {
				return withElided(true);
			}
			lineHeight = 0;
			last_rBearing = 0;// b->f_rbearing(); (0 for newline)
			last_rPadding = 0;// b->f_rpadding(); (0 for newline)

			initNextParagraph(i + 1, index);
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

		if (_btype == TextBlockType::Text) {
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

				if (lineElided) {
					lineHeight = qMax(lineHeight, blockHeight);
				} else if (f != j && !geometry.breakEverywhere) {
					j = f;
					widthLeft = f_wLeft;
					lineHeight = f_lineHeight;
					j_width = (j->f_width() >= 0) ? j->f_width() : -j->f_width();
				}

				callback(lineLeft + lineWidth - widthLeft, top += lineHeight);
				if (lineElided) {
					return withElided(true);
				}

				lineHeight = qMax(0, blockHeight);

				initNextLine();

				last_rBearing = j->f_rbearing();
				last_rPadding = j->f_rpadding();
				widthLeft -= j_width - last_rBearing;

				longWordLine = !wordEndsHere;
				f = j + 1;
				f_wLeft = widthLeft;
				f_lineHeight = lineHeight;
			}
			continue;
		}

		if (lineElided) {
			lineHeight = qMax(lineHeight, blockHeight);
		}
		callback(lineLeft + lineWidth - widthLeft, top += lineHeight);
		if (lineElided) {
			return withElided(true);
		}

		lineHeight = qMax(0, blockHeight);
		initNextLine();

		last_rBearing = b__f_rbearing;
		last_rPadding = b->f_rpadding();
		widthLeft -= b->f_width() - last_rBearing;

		longWordLine = true;
		continue;
	}
	if (widthLeft < lineWidth) {
		callback(
			lineLeft + lineWidth - widthLeft,
			top + lineHeight + qpadding.bottom());
	}
	return withElided(false);
}

void String::draw(QPainter &p, const PaintContext &context) const {
	Renderer(*this).draw(p, context);
}

StateResult String::getState(
		QPoint point,
		GeometryDescriptor geometry,
		StateRequest request) const {
	return Renderer(*this).getState(point, std::move(geometry), request);
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
		//.outerWidth = outerw,
		.availableWidth = width,
		.align = align,
		.clip = (yTo >= 0
			? QRect(left, top + yFrom, width, yTo - yFrom)
			: QRect()),
		.palette = &p.textPalette(),
		.paused = p.inactive(),
		.selection = selection,
	});
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
	if (isEmpty()) {
		return {};
	}
	return Renderer(*this).getState(
		point,
		SimpleGeometry(width, 0, 0, false),
		request);
}

StateResult String::getStateLeft(QPoint point, int width, int outerw, StateRequest request) const {
	return getState(style::rtlpoint(point, outerw), width, request);
}

StateResult String::getStateElided(QPoint point, int width, StateRequestElided request) const {
	if (isEmpty()) {
		return {};
	}
	return Renderer(*this).getState(point, SimpleGeometry(
		width,
		request.lines,
		request.removeFromEnd,
		request.flags & StateRequest::Flag::BreakEverywhere
	), static_cast<StateRequest>(request));
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
				if (b->position() < from) {
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
	return _blocks.empty() || _blocks[0]->type() == TextBlockType::Skip;
}

not_null<ExtendedData*> String::ensureExtended() {
	if (!_extended) {
		_extended = std::make_unique<ExtendedData>();
	}
	return _extended.get();
}

uint16 String::countBlockEnd(
		const TextBlocks::const_iterator &i,
		const TextBlocks::const_iterator &e) const {
	return (i + 1 == e) ? _text.size() : (*(i + 1))->position();
}

uint16 String::countBlockLength(
		const TextBlocks::const_iterator &i,
		const TextBlocks::const_iterator &e) const {
	return countBlockEnd(i, e) - (*i)->position();
}

QuoteDetails *String::quoteByIndex(int index) const {
	Expects(!index
		|| (_extended && index <= _extended->quotes.size()));

	return index ? &_extended->quotes[index - 1] : nullptr;
}

int String::quoteIndex(const AbstractBlock *block) const {
	Expects(!block || block->type() == TextBlockType::Newline);

	return block
		? static_cast<const NewlineBlock*>(block)->quoteIndex()
		: _startQuoteIndex;
}

const style::QuoteStyle &String::quoteStyle(
		not_null<QuoteDetails*> quote) const {
	return quote->pre ? _st->pre : _st->blockquote;
}

QMargins String::quotePadding(QuoteDetails *quote) const {
	if (!quote) {
		return {};
	}
	const auto &st = quoteStyle(quote);
	const auto skip = st.verticalSkip;
	const auto top = st.header;
	return st.padding + QMargins(0, top + skip, 0, skip);
}

int String::quoteMinWidth(QuoteDetails *quote) const {
	if (!quote) {
		return 0;
	}
	const auto qpadding = quotePadding(quote);
	const auto &qheader = quoteHeaderText(quote);
	const auto &qst = quoteStyle(quote);
	const auto radius = qst.radius;
	const auto header = qst.header;
	const auto outline = qst.outline;
	const auto iconsize = (!qst.icon.empty())
		? std::max(
			qst.icon.width() + qst.iconPosition.x(),
			qst.icon.height() + qst.iconPosition.y())
		: 0;
	const auto corner = std::max({ header, radius, outline, iconsize });
	const auto top = qpadding.left()
		+ (qheader.isEmpty()
			? 0
			: (_st->font->monospace()->width(qheader)
				+ _st->pre.headerPosition.x()))
		+ std::max(
			qpadding.right(),
			(!qst.icon.empty()
				? (qst.iconPosition.x() + qst.icon.width())
				: 0));
	return std::max(top, 2 * corner);
}

const QString &String::quoteHeaderText(QuoteDetails *quote) const {
	static const auto kEmptyHeader = QString();
	static const auto kDefaultHeader
		= Integration::Instance().phraseQuoteHeaderCopy();
	return (!quote || !quote->pre)
		? kEmptyHeader
		: quote->language.isEmpty()
		? kDefaultHeader
		: quote->language;
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

	int linkIndex = 0;
	uint16 linkPosition = 0;
	int quoteIndex = _startQuoteIndex;

	TextBlockFlags flags = {};
	for (auto i = _blocks.cbegin(), e = _blocks.cend(); true; ++i) {
		const auto blockPosition = (i == e)
			? uint16(_text.size())
			: (*i)->position();
		const auto blockFlags = (i == e) ? TextBlockFlags() : (*i)->flags();
		const auto blockQuoteIndex = (i == e)
			? 0
			: ((*i)->type() != TextBlockType::Newline)
			? quoteIndex
			: static_cast<const NewlineBlock*>(i->get())->quoteIndex();
		const auto blockLinkIndex = [&] {
			if (IsMono(blockFlags) || (i == e)) {
				return 0;
			}
			const auto result = (*i)->linkIndex();
			return (result && _extended && _extended->links[result - 1])
				? result
				: 0;
		}();
		if (blockLinkIndex != linkIndex) {
			if (linkIndex) {
				auto rangeFrom = qMax(selection.from, linkPosition);
				auto rangeTo = qMin(selection.to, blockPosition);
				if (rangeTo > rangeFrom) { // handle click handler
					const auto r = base::StringViewMid(
						_text,
						rangeFrom,
						rangeTo - rangeFrom);
					// Ignore links that are partially copied.
					const auto handler = (linkPosition != rangeFrom
						|| blockPosition != rangeTo
						|| !_extended)
						? nullptr
						: _extended->links[linkIndex - 1];
					const auto type = handler
						? handler->getTextEntity().type
						: EntityType::Invalid;
					clickHandlerFinishCallback(r, handler, type);
				}
			}
			linkIndex = blockLinkIndex;
			if (linkIndex) {
				linkPosition = blockPosition;
				const auto handler = _extended
					? _extended->links[linkIndex - 1]
					: nullptr;
				clickHandlerStartCallback(handler
					? handler->getTextEntity().type
					: EntityType::Invalid);
			}
		}

		const auto checkBlockFlags = (blockPosition >= selection.from)
			&& (blockPosition <= selection.to);
		if (checkBlockFlags
			&& (blockFlags != flags
				|| ((flags & TextBlockFlag::Pre)
					&& blockQuoteIndex != quoteIndex))) {
			flagsChangeCallback(
				flags,
				quoteIndex,
				blockFlags,
				blockQuoteIndex);
			flags = blockFlags;
		}
		quoteIndex = blockQuoteIndex;
		if (i == e
			|| (linkIndex ? linkPosition : blockPosition) >= selection.to) {
			break;
		}

		const auto blockType = (*i)->type();
		if (blockType == TextBlockType::Skip) {
			continue;
		}

		auto rangeFrom = qMax(selection.from, blockPosition);
		auto rangeTo = qMin(
			selection.to,
			uint16(blockPosition + countBlockLength(i, e)));
		if (rangeTo > rangeFrom) {
			const auto customEmojiData = (blockType == TextBlockType::CustomEmoji)
				? static_cast<const CustomEmojiBlock*>(i->get())->_custom->entityData()
				: QString();
			appendPartCallback(
				base::StringViewMid(_text, rangeFrom, rangeTo - rangeFrom),
				customEmojiData);
		}
	}
}

bool String::hasPersistentAnimation() const {
	return _hasCustomEmoji || hasSpoilers();
}

void String::unloadPersistentAnimation() {
	if (_hasCustomEmoji) {
		for (const auto &block : _blocks) {
			const auto raw = block.get();
			if (raw->type() == TextBlockType::CustomEmoji) {
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
		if (raw->type() == TextBlockType::CustomEmoji) {
			const auto custom = static_cast<const CustomEmojiBlock*>(raw);
			result.lines.back().push_back({
				.entityData = custom->_custom->entityData(),
			});
		} else if (raw->type() == TextBlockType::Newline) {
			result.lines.emplace_back();
		}
	}
	return result;
}

bool String::hasNotEmojiAndSpaces() const {
	return _hasNotEmojiAndSpaces;
}

const std::vector<Modification> &String::modifications() const {
	static const auto kEmpty = std::vector<Modification>();
	return _extended ? _extended->modifications : kEmpty;
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
	auto markdownTrackers = composeEntities
		? std::vector<MarkdownTagTracker>{
			{ TextBlockFlag::Italic, EntityType::Italic },
			{ TextBlockFlag::Bold, EntityType::Bold },
			{ TextBlockFlag::Semibold, EntityType::Semibold },
			{ TextBlockFlag::Underline, EntityType::Underline },
			{ TextBlockFlag::Spoiler, EntityType::Spoiler },
			{ TextBlockFlag::StrikeOut, EntityType::StrikeOut },
			{ TextBlockFlag::Code, EntityType::Code }, // #TODO entities
			{ TextBlockFlag::Pre, EntityType::Pre },
			{ TextBlockFlag::Blockquote, EntityType::Blockquote },
		} : std::vector<MarkdownTagTracker>();
	const auto flagsChangeCallback = [&](
			TextBlockFlags oldFlags,
			int oldQuoteIndex,
			TextBlockFlags newFlags,
			int newQuoteIndex) {
		if (!composeEntities) {
			return;
		}
		for (auto &tracker : markdownTrackers) {
			const auto flag = tracker.flag;
			const auto quoteWithLanguage = (flag == TextBlockFlag::Pre);
			const auto quoteWithLanguageChanged = quoteWithLanguage
				&& (oldQuoteIndex != newQuoteIndex);
			const auto data = (quoteWithLanguage && oldQuoteIndex)
				? _extended->quotes[oldQuoteIndex - 1].language
				: QString();
			if (((oldFlags & flag) && !(newFlags & flag))
				|| quoteWithLanguageChanged) {
				insertEntity({
					tracker.type,
					tracker.start,
					int(result.rich.text.size()) - tracker.start,
					data,
				});
			}
			if (((newFlags & flag) && !(oldFlags & flag))
				|| quoteWithLanguageChanged) {
				tracker.start = result.rich.text.size();
			}
		}
	};
	const auto clickHandlerStartCallback = [&](EntityType type) {
		linkStart = result.rich.text.size();
	};
	const auto clickHandlerFinishCallback = [&](
			QStringView inText,
			const ClickHandlerPtr &handler,
			EntityType type) {
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
		|| _blocks.back()->type() != TextBlockType::Skip) ? 0 : 1;
	if ((_blocks.size() > kIsolatedEmojiLimit + skip) || hasSpoilers()) {
		return {};
	}
	auto index = 0;
	for (const auto &block : _blocks) {
		const auto type = block->type();
		if (block->linkIndex()) {
			return {};
		} else if (type == TextBlockType::Emoji) {
			result.items[index++] = block.unsafe<EmojiBlock>()._emoji;
		} else if (type == TextBlockType::CustomEmoji) {
			result.items[index++]
				= block.unsafe<CustomEmojiBlock>()._custom->entityData();
		} else if (type != TextBlockType::Skip) {
			return {};
		}
	}
	return result;
}

void String::clear() {
	_text.clear();
	_blocks.clear();
	_extended = nullptr;
	_maxWidth = _minHeight = 0;
	_startQuoteIndex = 0;
	_startParagraphLTR = false;
	_startParagraphRTL = false;
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

bool IsDiacritic(QChar ch) { // diacritic and variation selectors
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
