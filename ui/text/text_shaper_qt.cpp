// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_shaper.h"

#include "ui/text/text_block.h"
#include "ui/text/text_bidi_algorithm.h"
#include "ui/text/text_stack_engine.h"

#include <private/qfontengine_p.h>

namespace Ui::Text {
namespace {

// Grapheme boundaries are where a caret may go, and a cluster is what the
// shaper drew as one glyph, so a cut must be at both.
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

// A cluster can map to several glyphs, and the first one's advance is not the
// width of the whole thing, so the advances of all of them are summed up.
[[nodiscard]] QFixed GlyphsAdvance(
		const QGlyphLayout &glyphs,
		int from,
		int till) {
	auto result = QFixed();
	for (auto i = from; i != till; ++i) {
		result += glyphs.effectiveAdvance(i);
	}
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

} // namespace

// Everything a shaped item is, kept where only this backend can see it.
struct ShapeEntry {
	QTextEngine *engine = nullptr;
	const QScriptItem *si = nullptr;

	// Both are shifted so that index zero is the first character of the item.
	const unsigned short *logClusters = nullptr;
	const QCharAttributes *attributes = nullptr;

	QGlyphLayout glyphs;
	int length = 0;
};

Fixed ShapedItem::width() const {
	return width(0, _length);
}

Fixed ShapedItem::width(int fromOffset, int tillOffset) const {
	if (fromOffset >= tillOffset) {
		// An empty range of characters is an empty range of glyphs, and not
		// the rest of the item - which is what the end of it maps to.
		return {};
	}
	const auto &entry = *_entry;
	const auto from = entry.logClusters[fromOffset];
	const auto till = (tillOffset < entry.length)
		? entry.logClusters[tillOffset]
		: entry.si->num_glyphs;
	return Fixed::FromRaw(GlyphsAdvance(entry.glyphs, from, till).value());
}

int ShapedItem::clusterEnd(int offset, int tillOffset) const {
	const auto &entry = *_entry;
	for (auto i = offset + 1; i < tillOffset; ++i) {
		if (entry.logClusters[i] != entry.logClusters[offset]) {
			return i;
		}
	}
	return tillOffset;
}

int ShapedItem::cutEnd(int offset, int tillOffset) const {
	const auto &entry = *_entry;
	auto result = GraphemeEnd(entry.attributes, offset, tillOffset);
	while (result < tillOffset
		&& entry.logClusters[result] == entry.logClusters[offset]) {
		result = GraphemeEnd(entry.attributes, result, tillOffset);
	}
	return result;
}

ShapedItem::Hit ShapedItem::hitTest(
		Fixed xFixed,
		int fromOffset,
		int tillOffset) const {
	const auto x = QFixed::fromFixed(xFixed.raw());
	const auto &entry = *_entry;
	const auto glyphsEnd = (tillOffset < entry.length)
		? entry.logClusters[tillOffset]
		: entry.si->num_glyphs;
	auto position = _rtl
		? QFixed::fromFixed(width(fromOffset, tillOffset).raw())
		: QFixed();
	auto letters = QVarLengthArray<int, 16>();
	for (auto ch = fromOffset; ch < tillOffset;) {
		// A caret only goes before or after a whole visible letter, and
		// letters that share a glyph - a ligature, a consonant with its matra
		// - have no width of their own, so they take an equal share of the one
		// they are drawn as, which is how a point maps back to a position.
		const auto clusterEnd = this->clusterEnd(ch, tillOffset);
		const auto width = GlyphsAdvance(
			entry.glyphs,
			entry.logClusters[ch],
			(clusterEnd < tillOffset)
				? entry.logClusters[clusterEnd]
				: glyphsEnd);
		letters.clear();
		for (auto i = ch; i != clusterEnd; ++i) {
			if (entry.attributes[i].graphemeBoundary) {
				letters.push_back(i);
			}
		}
		if (letters.isEmpty()) {
			letters.push_back(ch);
		}
		// Every boundary is measured from where the cluster begins. Adding an
		// equal share letter by letter would drop what the division leaves
		// over, and that remainder would push everything after it: a line of
		// ligatures walks away from the text it is being compared with.
		const auto count = int(letters.size());
		const auto origin = position;
		const auto at = [&](int part, int of) {
			const auto shift = width * part / of;
			return _rtl ? (origin - shift) : (origin + shift);
		};
		for (auto k = 0; k != count; ++k) {
			const auto from = letters[k];
			const auto till = (k + 1 != count) ? letters[k + 1] : clusterEnd;
			const auto edge = at(k + 1, count);
			const auto inside = _rtl ? (x >= edge) : (x < edge);
			if (inside) {
				const auto middle = at(2 * k + 1, 2 * count);
				const auto before = _rtl ? (x >= middle) : (x < middle);
				return { before ? from : (till - 1), !before };
			}
		}
		position = _rtl ? (origin - width) : (origin + width);
		ch = clusterEnd;
	}
	return (tillOffset > fromOffset)
		? Hit{ tillOffset - 1, true }
		: Hit{ fromOffset, false };
}

void ShapedItem::draw(
		QPainter &p,
		QPointF at,
		int fromOffset,
		int tillOffset,
		const QFont &font) const {
	const auto &entry = *_entry;
	if (fromOffset >= tillOffset) {
		return;
	}
	const auto glyphsStart = entry.logClusters[fromOffset];
	const auto glyphsEnd = (tillOffset < entry.length)
		? entry.logClusters[tillOffset]
		: entry.si->num_glyphs;
	if (glyphsEnd <= glyphsStart) {
		// A half of a middle elision can come out with no letter of its own:
		// the ellipsis is drawn either way, and half a letter is worse than
		// none.
		return;
	}
	auto &e = *entry.engine;
	if (e.fnt != font) {
		e.fnt = font;
		e.resetFontEngineCache();
	}
	auto item = QTextItemInt();
	item.glyphs = entry.glyphs.mid(glyphsStart, glyphsEnd - glyphsStart);
	item.f = &e.fnt;
	item.chars = e.layoutData->string.unicode()
		+ entry.si->position
		+ fromOffset;
	item.num_chars = tillOffset - fromOffset;
	item.fontEngine = e.fontEngine(*entry.si);
	item.logClusters = entry.logClusters + fromOffset;
	item.width = QFixed::fromFixed(width(fromOffset, tillOffset).raw());
	item.justified = false;
	InitTextItemWithScriptItem(item, *entry.si);
	if (!item.width) {
		item.flags &= ~QTextItem::Underline;
		item.underlineStyle = QTextCharFormat::NoUnderline;
	}
	p.drawTextItem(at, item);
}

bool ShapedItem::invisibleAt(int offset) const {
	const auto &entry = *_entry;
	if (offset < 0 || offset >= entry.length) {
		return false;
	}
	const auto glyph = entry.logClusters[offset];
	return (glyph < entry.si->num_glyphs)
		&& entry.glyphs.attributes[glyph].dontPrint;
}

Fixed ShapedItem::rightBearingBefore(int offset) const {
	const auto &entry = *_entry;
	if (offset <= 0 || offset > entry.length) {
		return {};
	}
	const auto glyph = entry.logClusters[offset - 1];
	if (glyph >= entry.si->num_glyphs) {
		return {};
	}
	auto bearing = qreal();
	entry.engine->fontEngine(*entry.si)->getGlyphBearings(
		entry.glyphs.glyphs[glyph],
		0,
		&bearing);
	// Right bearing is negative when a glyph sticks out to the right, and
	// only that case is interesting: nothing is gained by a glyph that ends
	// before its advance does.
	return Fixed::FromRaw(qMin(QFixed::fromReal(bearing), QFixed(0)).value());
}

struct Paragraph::State {
	QVarLengthArray<QScriptAnalysis, 4096> analysis;
	int position = 0;
};

Paragraph::Paragraph()
: _state(std::make_unique<State>()) {
}

Paragraph::Paragraph(Paragraph &&other) = default;

Paragraph &Paragraph::operator=(Paragraph &&other) = default;

Paragraph::~Paragraph() = default;

void Paragraph::clear() {
	_state->analysis.resize(0);
}

bool Paragraph::ready() const {
	return !_state->analysis.isEmpty();
}

void Paragraph::resolve(
		not_null<const String*> t,
		int position,
		int length,
		bool baseRtl,
		int blockIndexHint,
		int blockIndexLimit) {
	if (ready()) {
		return;
	}
	// Kept even for an empty paragraph: the lines of it still ask for their
	// place in the analysis, and the answer is "at the start of nothing".
	_state->position = position;
	if (!length) {
		return;
	}
	_state->analysis.resize(length);
	auto bidi = BidiAlgorithm(
		t->_text.constData() + position,
		_state->analysis.data(),
		length,
		baseRtl,
		begin(t->_blocks) + blockIndexHint,
		(blockIndexLimit >= 0)
			? (begin(t->_blocks) + blockIndexLimit)
			: end(t->_blocks),
		position);
	bidi.process();
}

void Paragraph::elide(int position, int count, bool baseRtl) {
	const auto length = position + count;
	if (length > _state->analysis.size()) {
		_state->analysis.resize(length);
	}
	const auto level = (length > count)
		? _state->analysis[length - count - 1].bidiLevel
		: uchar(baseRtl ? 1 : 0);
	for (auto i = count; i > 0; --i) {
		_state->analysis[length - i].bidiLevel = level;
	}
}

struct LineShaper::Backend {
	Backend(
		not_null<const String*> t,
		Paragraph &paragraph,
		int offset,
		const QString &text,
		int blockIndexHint,
		int blockIndexLimit)
	: engine(
		t,
		offset,
		text,
		gsl::span(paragraph._state->analysis).subspan(
			offset - paragraph._state->position),
		blockIndexHint,
		blockIndexLimit) {
	}

	StackEngine engine;
	int first = 0;
	std::vector<Item> items;
	std::vector<int> visualOrder;
	std::vector<CharAttribute> attributes;
	std::vector<ShapeEntry> entries;
	std::vector<ShapedItem> shaped;
};

LineShaper::LineShaper(
	not_null<const String*> t,
	Paragraph &paragraph,
	int offset,
	const QString &text,
	int blockIndexHint,
	int blockIndexLimit)
: _backend(std::make_unique<Backend>(
	t,
	paragraph,
	offset,
	text,
	blockIndexHint,
	blockIndexLimit)) {
}

LineShaper::~LineShaper() = default;

const std::vector<Item> &LineShaper::items() const {
	return _backend->items;
}

gsl::span<const CharAttribute> LineShaper::attributes() const {
	auto &attributes = _backend->attributes;
	if (attributes.empty()) {
		auto &e = _backend->engine.wrapped();
		// Taken after shaping: shaping grows the engine's memory block, and
		// the attributes live in it, so a pointer taken earlier can be left
		// behind.
		if (const auto from = e.attributes()) {
			const auto count = int(e.layoutData->string.size());
			attributes.resize(count);
			for (auto i = 0; i != count; ++i) {
				attributes[i] = CharAttribute{
					.graphemeBoundary = bool(from[i].graphemeBoundary),
					.lineBreak = bool(from[i].lineBreak),
					.whiteSpace = bool(from[i].whiteSpace),
				};
			}
		}
	}
	return attributes;
}

gsl::span<const int> LineShaper::visualOrder() const {
	return _backend->visualOrder;
}

int LineShaper::findItem(int position) const {
	return _backend->engine.wrapped().findItem(position);
}

void LineShaper::shapeRange(int firstItem, int lastItem) {
	auto &e = _backend->engine.wrapped();
	_backend->first = firstItem;
	const auto count = lastItem - firstItem + 1;
	_backend->items.resize(count);
	_backend->entries.resize(count);
	_backend->shaped.resize(count);

	auto levels = QVarLengthArray<uchar>(count);
	auto skipIndex = -1;
	for (auto i = 0; i != count; ++i) {
		const auto blockIt = _backend->engine.shapeGetBlock(firstItem + i);
		auto &si = e.layoutData->items[firstItem + i];
		if ((*blockIt)->type() == TextBlockType::Skip) {
			// A skip is laid out as if it had no direction of its own, so
			// that it stays at the end of the line it trails.
			levels[i] = si.analysis.bidiLevel = 0;
			skipIndex = i;
		} else {
			levels[i] = si.analysis.bidiLevel;
		}
		_backend->items[i] = Item{
			.position = si.position,
			.length = itemLength(firstItem + i),
			.width = Fixed::FromRaw(si.width.value()),
			.ascent = si.ascent.toInt(),
			.descent = si.descent.toInt(),
			.blockIndex = _backend->engine.blockIndex(si.position),
			.bidiLevel = uchar(si.analysis.bidiLevel),
			.object = (si.analysis.flags >= QScriptAnalysis::TabOrObject),
			.newline = (si.analysis.flags
				== QScriptAnalysis::LineOrParagraphSeparator),
		};
	}

	_backend->visualOrder.resize(count);
	QTextEngine::bidiReorder(count, levels.data(), _backend->visualOrder.data());
	if (style::RightToLeft() && skipIndex == count - 1) {
		for (auto i = count; i > 1;) {
			--i;
			_backend->visualOrder[i] = _backend->visualOrder[i - 1];
		}
		_backend->visualOrder[0] = skipIndex;
	}
}

int LineShaper::itemLength(int index) const {
	auto &e = _backend->engine.wrapped();
	const auto &list = e.layoutData->items;
	const auto next = (index + 1 < int(list.size()))
		? list[index + 1].position
		: int(e.layoutData->string.size());
	return next - list[index].position;
}

const ShapedItem &LineShaper::shape(int index) {
	auto &e = _backend->engine.wrapped();
	auto &shaped = _backend->shaped[index];
	if (shaped) {
		return shaped;
	}
	auto &si = e.layoutData->items[_backend->first + index];
	auto &entry = _backend->entries[index];

	// Asked for first: filling them in grows the engine's memory block, and
	// the glyphs live in it, so pointers taken earlier would be left behind.
	const auto attributes = e.attributes();

	entry.engine = &e;
	entry.si = &si;
	entry.logClusters = e.logClusters(&si);
	entry.glyphs = e.shapedGlyphs(&si);
	entry.attributes = attributes ? (attributes + si.position) : nullptr;
	entry.length = itemLength(_backend->first + index);
	shaped._entry = &entry;
	shaped._length = entry.length;
	shaped._rtl = (si.analysis.bidiLevel % 2) != 0;
	return shaped;
}

int LineShaper::blockIndex(int position) const {
	return _backend->engine.blockIndex(position);
}

} // namespace Ui::Text
