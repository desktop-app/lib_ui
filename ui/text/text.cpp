// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text.h"

#include "ui/effects/spoiler_mess.h"
#include "ui/text/text_block_parser.h"
#include "ui/text/text_extended_data.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/text_renderer.h"
#include "ui/text/text_word_parser.h"
#include "ui/basic_click_handlers.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_basic.h"

#include <QtGui/QGuiApplication>

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
	const auto expand = st.expand.empty() ? nullptr : &st.expand;
	const auto collapse = st.collapse.empty() ? nullptr : &st.collapse;
	if (!cache.corners.isNull()
		&& cache.bgCached == cache.bg
		&& cache.outlines == cache.outlines
		&& (!st.header || cache.headerCached == cache.header)
		&& ((!icon && !expand && !collapse)
			|| cache.iconCached == cache.icon)) {
		return;
	}
	cache.bgCached = cache.bg;
	cache.outlinesCached = cache.outlines;
	if (st.header) {
		cache.headerCached = cache.header;
	}
	if (icon || expand || collapse) {
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
	cache.expand = expand ? expand->instance(cache.icon) : QImage();
	cache.collapse = collapse ? collapse->instance(cache.icon) : QImage();
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
	const auto till = y + height;
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
		rect.setHeight(height);
	}
	if (outline && height > 0) {
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
	const auto icon = parts.expandIcon
		? &cache.expand
		: parts.collapseIcon
		? &cache.collapse
		: nullptr;
	if (icon && !icon->isNull()) {
		const auto position = parts.expandIcon
			? st.expandPosition
			: st.collapsePosition;
		const auto size = icon->size() / icon->devicePixelRatio();
		p.drawImage(
			QRect(
				x + width - size.width() - position.x(),
				till - size.height() - position.y(),
				size.width(),
				size.height()),
			*icon);
	}
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
	if (!data) {
		return;
	}
	const auto raw = [](auto pointer) {
		return reinterpret_cast<quintptr>(pointer);
	};
	const auto adjust = [&](auto &link) {
		const auto otherText = raw(link->text().get());
		link->setText(
			reinterpret_cast<String*>(otherText + raw(this) - raw(other)));
	};
	if (const auto spoiler = data->spoiler.get()) {
		if (spoiler->link) {
			adjust(spoiler->link);
		}
	}
	if (const auto quotes = data->quotes.get()) {
		for (auto &quote : quotes->list) {
			if (quote.copy) {
				adjust(quote.copy);
			}
			if (quote.toggle) {
				adjust(quote.toggle);
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
	const MarkedContext &context)
: _minResizeWidth(minResizeWidth) {
	setMarkedText(st, textWithEntities, options, context);
}


String::String(String &&other) = default;

String &String::operator=(String &&other) = default;

String::~String() = default;

void String::setText(const style::TextStyle &st, const QString &text, const TextParseOptions &options) {
	setMarkedText(st, { text }, options);
}

void String::recountNaturalSize(
		bool initial,
		Qt::LayoutDirection optionsDirection) {
	auto lastNewlineBlock = begin(_blocks);
	auto lastNewlineStart = 0;
	const auto computeParagraphDirection = [&](int paragraphEnd) {
		const auto direction = (optionsDirection != Qt::LayoutDirectionAuto)
			? optionsDirection
			: StringDirection(_text, lastNewlineStart, paragraphEnd);

		if (paragraphEnd) {
			while (blockPosition(lastNewlineBlock) < lastNewlineStart) {
				++lastNewlineBlock;
			}
			Assert(lastNewlineBlock != end(_blocks));
			const auto block = lastNewlineBlock->get();
			if (block->type() == TextBlockType::Newline) {
				Assert(block->position() == lastNewlineStart);
				static_cast<NewlineBlock*>(block)->setParagraphDirection(
					direction);
			} else {
				Assert(!lastNewlineStart);
				_startParagraphLTR = (direction == Qt::LeftToRight);
				_startParagraphRTL = (direction == Qt::RightToLeft);
			}
		}
	};

	auto qindex = quoteIndex(nullptr);
	auto quote = quoteByIndex(qindex);
	auto qpadding = quotePadding(quote);
	auto qminwidth = quoteMinWidth(quote);
	auto qlinesleft = quoteLinesLimit(quote);
	auto qmaxwidth = QFixed(qminwidth);
	auto qoldheight = 0;

	_maxWidth = 0;
	_minHeight = qpadding.top();
	const auto lineHeight = this->lineHeight();
	auto maxWidth = QFixed();
	auto width = QFixed(qminwidth);
	auto last_rBearing = QFixed();
	auto last_rPadding = QFixed();
	for (const auto &word : _words) {
		if (word.newline()) {
			const auto block = word.newlineBlockIndex();
			const auto index = quoteIndex(_blocks[block].get());
			const auto changed = (qindex != index);
			const auto hidden = !qlinesleft;
			accumulate_max(maxWidth, width);
			accumulate_max(qmaxwidth, width);

			if (changed) {
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
				qlinesleft = quoteLinesLimit(quote);
				qmaxwidth = qminwidth;
				_minHeight += qpadding.top();
				qpadding.setTop(0);
			} else if (qlinesleft > 0) {
				--qlinesleft;
			}
			if (initial) {
				computeParagraphDirection(word.position());
			}
			lastNewlineStart = word.position();

			if (!hidden) {
				_minHeight += lineHeight;
			}
			last_rBearing = 0;// b->f_rbearing(); (0 for newline)
			last_rPadding = word.f_rpadding();

			width = qminwidth;
			// + (b->f_width() - last_rBearing); (0 for newline)
			continue;
		}

		auto w__f_rbearing = word.f_rbearing(); // cache

		// We need to accumulate max width after each block, because
		// some blocks have width less than -1 * previous right bearing.
		// In that cases the _width gets _smaller_ after moving to the next block.
		//
		// But when we layout block and we're sure that _maxWidth is enough
		// for all the blocks to fit on their line we check each block, even the
		// intermediate one with a large negative right bearing.
		accumulate_max(maxWidth, width);
		accumulate_max(qmaxwidth, width);

		width += last_rBearing + (last_rPadding + word.f_width() - w__f_rbearing);

		last_rBearing = w__f_rbearing;
		last_rPadding = word.f_rpadding();
	}
	if (initial) {
		computeParagraphDirection(_text.size());
	}
	if (width > 0) {
		const auto useSkipHeight = (_blocks.back()->type() == TextBlockType::Skip)
			&& (_words.back().f_width() == width);
		_minHeight += qpadding.top() + qpadding.bottom();
		if (qlinesleft != 0) {
			_minHeight += useSkipHeight
				? _blocks.back().unsafe<SkipBlock>().height()
				: lineHeight;
		}
		accumulate_max(maxWidth, width);
		accumulate_max(qmaxwidth, width);
	}
	_maxWidth = maxWidth.ceil().toInt();
	if (quote) {
		quote->maxWidth = qmaxwidth.ceil().toInt();
		quote->minHeight = _minHeight - qoldheight;
		_endsWithQuoteOrOtherDirection = true;
	} else {
		const auto lastIsNewline = (lastNewlineBlock != end(_blocks))
			&& (lastNewlineBlock->get()->type() == TextBlockType::Newline);
		const auto lastNewline = lastIsNewline
			? static_cast<NewlineBlock*>(lastNewlineBlock->get())
			: nullptr;
		const auto lastLineDirection = lastNewline
			? lastNewline->paragraphDirection()
			: _startParagraphRTL
			? Qt::RightToLeft
			: Qt::LeftToRight;
		_endsWithQuoteOrOtherDirection
			= ((lastLineDirection != style::LayoutDirection())
				&& (lastLineDirection != Qt::LayoutDirectionAuto));
	}
}

int String::countMaxMonospaceWidth() const {
	auto result = 0;
	if (const auto quotes = _extended ? _extended->quotes.get() : nullptr) {
		for (const auto &quote : quotes->list) {
			if (quote.pre) {
				accumulate_max(result, quote.maxWidth);
			}
		}
	}
	return result;
}

void String::setMarkedText(
		const style::TextStyle &st,
		const TextWithEntities &textWithEntities,
		const TextParseOptions &options,
		const MarkedContext &context) {
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
//		BlockParser block(this, { newText, EntitiesInText() }, options, context);

		BlockParser block(this, textWithEntities, options, context);
		WordParser word(this);
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

void String::setBlockquoteExpandCallback(
		Fn<void(int index, bool expanded)> callback) {
	Expects(_extended && _extended->quotes);

	_extended->quotes->expandCallback = std::move(callback);
}

bool String::hasLinks() const {
	return _extended && !_extended->links.empty();
}

bool String::hasSpoilers() const {
	return _extended && (_extended->spoiler != nullptr);
}

bool String::hasCollapsedBlockquots() const {
	return _extended
		&& _extended->quotes
		&& ranges::any_of(_extended->quotes->list, &QuoteDetails::collapsed);
}

bool String::blockquoteCollapsed(int index) const {
	Expects(_extended && _extended->quotes);
	Expects(index > 0 && index <= _extended->quotes->list.size());

	return _extended->quotes->list[index - 1].collapsed;
}

bool String::blockquoteExpanded(int index) const {
	Expects(_extended && _extended->quotes);
	Expects(index > 0 && index <= _extended->quotes->list.size());

	return _extended->quotes->list[index - 1].expanded;
}

void String::setBlockquoteExpanded(int index, bool expanded) {
	Expects(_extended && _extended->quotes);
	Expects(index > 0 && index <= _extended->quotes->list.size());

	auto &quote = _extended->quotes->list[index - 1];
	if (quote.expanded == expanded) {
		return;
	}
	quote.expanded = expanded;
	recountNaturalSize(false);
	if (const auto onstack = _extended->quotes->expandCallback) {
		onstack(index, expanded);
	}
}

bool String::hasSkipBlock() const {
	return !_blocks.empty()
		&& (_blocks.back()->type() == TextBlockType::Skip);
}

bool String::updateSkipBlock(int width, int height) {
	if (!width || !height) {
		return removeSkipBlock();
	}
	if (!_blocks.empty() && _blocks.back()->type() == TextBlockType::Skip) {
		const auto &block = _blocks.back().unsafe<SkipBlock>();
		if (block.width() == width && block.height() == height) {
			return false;
		}
		const auto size = block.position();
		_text.resize(size);
		_blocks.pop_back();
		_words.pop_back();
		removeModificationsAfter(size);
	} else if (_endsWithQuoteOrOtherDirection) {
		insertModifications(_text.size(), 1);
		_words.push_back(Word(
			uint16(_text.size()),
			int(_blocks.size())));
		_blocks.push_back(Block::Newline({
			.position = _words.back().position(),
		}, 0));
		_text.push_back(QChar::LineFeed);
		_skipBlockAddedNewline = true;
	}
	insertModifications(_text.size(), 1);
	const auto unfinished = false;
	const auto rbearing = 0;
	_words.push_back(Word(
		uint16(_text.size()),
		unfinished,
		width,
		rbearing));
	_blocks.push_back(Block::Skip({
		.position = _words.back().position(),
	}, width, height));
	_text.push_back('_');
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
		_words.pop_back();
		_words.pop_back();
		_skipBlockAddedNewline = false;
		removeModificationsAfter(size);
	} else {
		const auto size = _blocks.back()->position();
		_text.resize(size);
		_blocks.pop_back();
		_words.pop_back();
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
	auto qlinesleft = -1;
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
	const auto initNextParagraph = [&](int16 paragraphIndex) {
		if (qindex != paragraphIndex) {
			//top += qpadding.bottom(); // This was done before callback().
			qindex = paragraphIndex;
			quote = quoteByIndex(qindex);
			qpadding = quotePadding(quote);
			qlinesleft = quoteLinesLimit(quote);
			top += qpadding.top();
			qpadding.setTop(0);
		}
		initNextLine();
	};

	if ((*_blocks.cbegin())->type() != TextBlockType::Newline) {
		initNextParagraph(_startQuoteIndex);
	}

	const auto lineHeight = this->lineHeight();
	auto last_rBearing = QFixed();
	auto last_rPadding = QFixed();
	auto longWordLine = true;
	auto lastWordStart = begin(_words);
	auto lastWordStart_wLeft = widthLeft;
	for (auto w = lastWordStart, e = end(_words); w != e; ++w) {
		if (w->newline()) {
			const auto block = w->newlineBlockIndex();
			const auto index = quoteIndex(_blocks[block].get());
			const auto hidden = !qlinesleft;
			const auto changed = (qindex != index);
			if (changed) {
				top += qpadding.bottom();
			}

			if (qlinesleft > 0) {
				--qlinesleft;
			}
			if (!hidden) {
				callback(lineLeft + lineWidth - widthLeft, top += lineHeight);
			}
			if (lineElided) {
				return withElided(true);
			}

			last_rBearing = 0;// b->f_rbearing(); (0 for newline)
			last_rPadding = w->f_rpadding();

			initNextParagraph(index);
			longWordLine = true;
			lastWordStart = w;
			lastWordStart_wLeft = widthLeft;
			continue;
		} else if (!qlinesleft) {
			continue;
		}
		const auto wordEndsHere = !w->unfinished();

		auto w__f_width = w->f_width();
		const auto w__f_rbearing = w->f_rbearing();
		const auto newWidthLeft = widthLeft
			- last_rBearing
			- (last_rPadding + w__f_width - w__f_rbearing);
		if (newWidthLeft >= 0) {
			last_rBearing = w__f_rbearing;
			last_rPadding = w->f_rpadding();
			widthLeft = newWidthLeft;

			if (wordEndsHere) {
				longWordLine = false;
			}
			if (wordEndsHere || longWordLine) {
				lastWordStart_wLeft = widthLeft;
				lastWordStart = w + 1;
			}
			continue;
		}

		if (lineElided) {
		} else if (w != lastWordStart && !geometry.breakEverywhere) {
			w = lastWordStart;
			widthLeft = lastWordStart_wLeft;
			w__f_width = w->f_width();
		}

		if (qlinesleft > 0) {
			--qlinesleft;
		}
		callback(lineLeft + lineWidth - widthLeft, top += lineHeight);
		if (lineElided) {
			return withElided(true);
		}

		initNextLine();

		last_rBearing = w->f_rbearing();
		last_rPadding = w->f_rpadding();
		widthLeft -= w__f_width - last_rBearing;

		longWordLine = !wordEndsHere;
		lastWordStart = w + 1;
		lastWordStart_wLeft = widthLeft;
	}
	if (widthLeft < lineWidth) {
		const auto useSkipHeight = (_blocks.back()->type() == TextBlockType::Skip)
			&& (widthLeft + _words.back().f_width() == lineWidth);
		const auto useLineHeight = useSkipHeight
			? _blocks.back().unsafe<SkipBlock>().height()
			: lineHeight;
		callback(
			lineLeft + lineWidth - widthLeft,
			top + useLineHeight + qpadding.bottom());
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
		.fullWidthSelection = fullWidthSelection,
		.selection = selection,
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

not_null<QuotesData*> String::ensureQuotes() {
	const auto extended = ensureExtended();
	if (!extended->quotes) {
		extended->quotes = std::make_unique<QuotesData>();
	}
	return extended->quotes.get();
}

uint16 String::blockPosition(
		std::vector<Block>::const_iterator i,
		int fullLengthOverride) const {
	return (i != end(_blocks))
		? CountPosition(i)
		: (fullLengthOverride >= 0)
		? uint16(fullLengthOverride)
		: uint16(_text.size());
}

uint16 String::blockEnd(
		std::vector<Block>::const_iterator i,
		int fullLengthOverride) const {
	return (i != end(_blocks) && i + 1 != end(_blocks))
		? CountPosition(i + 1)
		: (fullLengthOverride >= 0)
		? uint16(fullLengthOverride)
		: uint16(_text.size());
}

uint16 String::blockLength(
		std::vector<Block>::const_iterator i,
		int fullLengthOverride) const {
	return (i == end(_blocks))
		? 0
		: (i + 1 != end(_blocks))
		? (CountPosition(i + 1) - CountPosition(i))
		: (fullLengthOverride >= 0)
		? (fullLengthOverride - CountPosition(i))
		: (int(_text.size()) - CountPosition(i));
}

QuoteDetails *String::quoteByIndex(int index) const {
	Expects(!index
		|| (_extended
			&& _extended->quotes
			&& index <= _extended->quotes->list.size()));

	return index ? &_extended->quotes->list[index - 1] : nullptr;
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

int String::quoteLinesLimit(QuoteDetails *quote) const {
	return (quote && quote->collapsed && !quote->expanded)
		? kQuoteCollapsedLines
		: -1;
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
			uint16(blockPosition + blockLength(i)));
		if (rangeTo > rangeFrom) {
			const auto customEmojiData = (blockType == TextBlockType::CustomEmoji)
				? static_cast<const CustomEmojiBlock*>(i->get())->custom()->entityData()
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
				static_cast<const CustomEmojiBlock*>(raw)->custom()->unload();
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
				.entityData = custom->custom()->entityData(),
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
	using Flag = TextBlockFlag;
	using Flags = TextBlockFlags;
	auto linkStart = 0;
	auto markdownTrackers = composeEntities
		? std::vector<MarkdownTagTracker>{
			{ Flag::Italic, EntityType::Italic },
			{ Flag::Bold, EntityType::Bold },
			{ Flag::Semibold, EntityType::Semibold },
			{ Flag::Underline, EntityType::Underline },
			{ Flag::Spoiler, EntityType::Spoiler },
			{ Flag::StrikeOut, EntityType::StrikeOut },
			{ Flag::Code, EntityType::Code },
			{ Flag::Pre, EntityType::Pre },
			{ Flag::Blockquote, EntityType::Blockquote },
		} : std::vector<MarkdownTagTracker>();
	const auto flagsChangeCallback = [&](
			Flags oldFlags,
			int oldQuoteIndex,
			Flags newFlags,
			int newQuoteIndex) {
		if (!composeEntities) {
			return;
		}
		for (auto &tracker : markdownTrackers) {
			const auto flag = tracker.flag;
			const auto quoteWithCollapseChanged = (flag == Flag::Blockquote)
				&& (oldFlags & flag)
				&& (newFlags & flag)
				&& (oldQuoteIndex != newQuoteIndex);
			const auto quoteWithLanguageChanged = (flag == Flag::Pre)
				&& (oldFlags & flag)
				&& (newFlags & flag)
				&& (oldQuoteIndex != newQuoteIndex);
			const auto quote = !oldQuoteIndex
				? nullptr
				: &_extended->quotes->list[oldQuoteIndex - 1];
			const auto data = !quote
				? QString()
				: quote->pre
				? quote->language
				: quote->blockquote
				? (quote->collapsed ? u"1"_q : QString())
				: QString();
			if (((oldFlags & flag) && !(newFlags & flag))
				|| quoteWithLanguageChanged
				|| quoteWithCollapseChanged) {
				insertEntity({
					tracker.type,
					tracker.start,
					int(result.rich.text.size()) - tracker.start,
					data,
				});
			}
			if (((newFlags & flag) && !(oldFlags & flag))
				|| quoteWithLanguageChanged
				|| quoteWithCollapseChanged) {
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
			|| (entity.type == EntityType::Email)
			|| (entity.type == EntityType::BankCard)
			|| (entity.type == EntityType::Phone);
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
			result.items[index++] = block.unsafe<EmojiBlock>().emoji();
		} else if (type == TextBlockType::CustomEmoji) {
			result.items[index++]
				= block.unsafe<CustomEmojiBlock>().custom()->entityData();
		} else if (type != TextBlockType::Skip) {
			return {};
		}
	}
	return result;
}

int String::lineHeight() const {
	return _st->lineHeight ? _st->lineHeight : _st->font->height;
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
	return (ch.unicode() == 0)
		|| (ch.unicode() >= 8232 && ch.unicode() < 8237)
		|| (ch.unicode() >= 65024 && ch.unicode() < 65040 && ch.unicode() != 65039)
		|| (ch.unicode() >= 127 && ch.unicode() < 160 && ch.unicode() != 156)

		// qt harfbuzz crash see https://github.com/telegramdesktop/tdesktop/issues/4551
		|| (Platform::IsMac() && ch.unicode() == 6158);
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
		|| (ch.unicode() == 156);
}

bool IsSpace(QChar ch) {
	return ch.isSpace()
		|| (ch.unicode() < 32)
		|| (ch == QChar::ParagraphSeparator)
		|| (ch == QChar::LineSeparator)
		|| (ch == QChar::ObjectReplacementCharacter)
		|| (ch == QChar::CarriageReturn)
		|| (ch == QChar::Tabulation);
}

bool IsDiacritic(QChar ch) { // diacritic and variation selectors
	return (ch.category() == QChar::Mark_NonSpacing)
		|| (ch.unicode() == 1652)
		|| (ch.unicode() >= 64606 && ch.unicode() <= 64611);
}

bool IsReplacedBySpace(QChar ch) {
	// Those symbols are replaced by space on the Telegram server,
	// so we replace them as well, for sent / received consistency.
	//
	// \xe2\x80[\xa8 - \xac\xad] // 8232 - 8237
	// QString from1 = QString::fromUtf8("\xe2\x80\xa8"), to1 = QString::fromUtf8("\xe2\x80\xad");
	// \xcc[\xb3\xbf\x8a] // 819, 831, 778
	// QString bad1 = QString::fromUtf8("\xcc\xb3"), bad2 = QString::fromUtf8("\xcc\xbf"), bad3 = QString::fromUtf8("\xcc\x8a");
	// [\x00\x01\x02\x07\x08\x0b-\x1f] // '\t' = 0x09
	return (/*code >= 0x00 && */ch.unicode() <= 0x02)
		|| (ch.unicode() >= 0x07 && ch.unicode() <= 0x09)
		|| (ch.unicode() >= 0x0b && ch.unicode() <= 0x1f)
		|| (ch.unicode() == 819)
		|| (ch.unicode() == 831)
		|| (ch.unicode() == 778)
		|| (ch.unicode() >= 8232 && ch.unicode() <= 8237);
}

bool IsTrimmed(QChar ch) {
	return IsSpace(ch)
		|| IsBad(ch)
		|| (ch == QChar(8203)); // zero width space
}

} // namespace Ui::Text
