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

#include <new>
#include <type_traits>

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

// WHY: taken when an item is handed out and kept with it: by then the line
// is shaped and its attributes are computed, which is all that moves the
// memory of the engine - and the glyphs with it.
struct ItemView {
	QTextEngine *engine = nullptr;
	const QScriptItem *si = nullptr;

	// Both are shifted so that index zero is the first character of the item.
	const unsigned short *logClusters = nullptr;
	const QCharAttributes *attributes = nullptr;

	QGlyphLayout glyphs;
	int length = 0;
};

[[nodiscard]] const ItemView &View(const std::byte *storage) {
	return *std::launder(reinterpret_cast<const ItemView*>(storage));
}

} // namespace

// Counted from the bytes Paragraph has room for: QScriptAnalysis is 8 in MSVC
constexpr auto kInlineAnalysis = (16 * 1024) / sizeof(QScriptAnalysis);

struct Paragraph::State {
	QVarLengthArray<QScriptAnalysis, kInlineAnalysis> analysis;
	int position = 0;
};

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
		gsl::span(paragraph.state().analysis).subspan(
			offset - paragraph.state().position),
		blockIndexHint,
		blockIndexLimit) {
	}

	[[nodiscard]] int itemLength(int item) const;
	[[nodiscard]] ItemView view(int index);

	StackEngine engine;
	int first = 0;
	int count = 0;
	QVarLengthArray<int> visualOrder;
	QVarLengthArray<int> blockIndices;
	const QCharAttributes *attributes = nullptr;

	// The one this item was shaped with, kept because the engine moves its
	// font on to the next block and would hand out a different one later.
	// A glyph of a font that merges fallbacks carries the index of the one it
	// came from, and that index means nothing to any other font's engine.
	QVarLengthArray<QExplicitlySharedDataPointer<QFontEngine>> fontEngines;
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
	const auto &entry = View(_view);
	const auto from = entry.logClusters[fromOffset];
	const auto till = (tillOffset < entry.length)
		? entry.logClusters[tillOffset]
		: entry.si->num_glyphs;
	return Fixed::FromRaw(GlyphsAdvance(entry.glyphs, from, till).value());
}

int ShapedItem::clusterEnd(int offset, int tillOffset) const {
	const auto &entry = View(_view);
	for (auto i = offset + 1; i < tillOffset; ++i) {
		if (entry.logClusters[i] != entry.logClusters[offset]) {
			return i;
		}
	}
	return tillOffset;
}

int ShapedItem::cutEnd(int offset, int tillOffset) const {
	Expects(View(_view).attributes != nullptr);

	const auto &entry = View(_view);
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
	Expects(View(_view).attributes != nullptr);

	const auto x = QFixed::fromFixed(xFixed.raw());
	const auto &entry = View(_view);
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
	if (fromOffset >= tillOffset) {
		return;
	}
	const auto &entry = View(_view);
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
	const auto &entry = View(_view);
	if (offset < 0 || offset >= entry.length) {
		return false;
	}
	const auto glyph = entry.logClusters[offset];
	return (glyph < entry.si->num_glyphs)
		&& entry.glyphs.attributes[glyph].dontPrint;
}

Fixed ShapedItem::rightBearingBefore(int offset) const {
	const auto &entry = View(_view);
	if (offset <= 0 || offset > entry.length) {
		return {};
	}
	const auto glyph = entry.logClusters[offset - 1];
	const auto &fontEngine = _shaper->backend().fontEngines[_index];
	if (glyph >= entry.si->num_glyphs || !fontEngine) {
		return {};
	}
	auto bearing = qreal();
	fontEngine->getGlyphBearings(
		entry.glyphs.glyphs[glyph],
		0,
		&bearing);
	// Right bearing is negative when a glyph sticks out to the right, and
	// only that case is interesting: nothing is gained by a glyph that ends
	// before its advance does.
	return Fixed::FromRaw(
		std::min(QFixed::fromReal(bearing), QFixed(0)).value());
}

Paragraph::Paragraph() {
	static_assert(sizeof(State) <= sizeof(_storage));
	static_assert(alignof(State) <= alignof(std::max_align_t));
	new (_storage) State;
}

Paragraph::~Paragraph() {
	state().~State();
}

Paragraph::State &Paragraph::state() {
	return *std::launder(reinterpret_cast<State*>(_storage));
}

const Paragraph::State &Paragraph::state() const {
	return *std::launder(reinterpret_cast<const State*>(_storage));
}

void Paragraph::clear() {
	state().analysis.resize(0);
}

bool Paragraph::ready() const {
	return !state().analysis.isEmpty();
}

void Paragraph::resolve(
		not_null<const String*> t,
		int position,
		int length,
		bool baseRtl,
		int blockIndexHint,
		int blockIndexLimit,
		qreal ratio) {
	// The private engine lays out and draws in logical units either way, so
	// the ratio of the device is not its business.
	if (ready()) {
		return;
	}
	// Kept even for an empty paragraph: the lines of it still ask for their
	// place in the analysis, and the answer is "at the start of nothing".
	state().position = position;
	if (!length) {
		return;
	}
	auto &analysis = state().analysis;
	analysis.resize(length);
	auto bidi = BidiAlgorithm(
		t->_text.constData() + position,
		analysis.data(),
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
	auto &analysis = state().analysis;
	const auto length = position + count;
	if (length > analysis.size()) {
		analysis.resize(length);
	}
	const auto level = (length > count)
		? analysis[length - count - 1].bidiLevel
		: uchar(baseRtl ? 1 : 0);
	for (auto i = count; i > 0; --i) {
		analysis[length - i].bidiLevel = level;
	}
}

int LineShaper::Backend::itemLength(int item) const {
	const auto &e = engine.wrapped();
	const auto &list = e.layoutData->items;
	const auto next = (item + 1 < int(list.size()))
		? list[item + 1].position
		: int(e.layoutData->string.size());
	return next - list[item].position;
}

ItemView LineShaper::Backend::view(int index) {
	auto &e = engine.wrapped();
	const auto &si = e.layoutData->items[first + index];
	return {
		.engine = &e,
		.si = &si,
		.logClusters = e.logClusters(&si),
		.attributes = attributes ? (attributes + si.position) : nullptr,
		.glyphs = e.shapedGlyphs(&si),
		.length = itemLength(first + index),
	};
}

LineShaper::LineShaper(
	not_null<const String*> t,
	Paragraph &paragraph,
	int offset,
	const QString &text,
	int blockIndexHint,
	int blockIndexLimit) {
	static_assert(sizeof(Backend) <= sizeof(_storage));
	static_assert(alignof(Backend) <= alignof(std::max_align_t));
	new (_storage) Backend(
		t,
		paragraph,
		offset,
		text,
		blockIndexHint,
		blockIndexLimit);
}

LineShaper::~LineShaper() {
	backend().~Backend();
}

LineShaper::Backend &LineShaper::backend() {
	return *std::launder(reinterpret_cast<Backend*>(_storage));
}

const LineShaper::Backend &LineShaper::backend() const {
	return *std::launder(reinterpret_cast<const Backend*>(_storage));
}

int LineShaper::itemCount() const {
	return backend().count;
}

Item LineShaper::item(int index) const {
	const auto &backend = this->backend();
	const auto item = backend.first + index;
	const auto &si = backend.engine.wrapped().layoutData->items[item];
	return {
		.position = si.position,
		.length = backend.itemLength(item),
		.width = Fixed::FromRaw(si.width.value()),
		.ascent = si.ascent.toInt(),
		.descent = si.descent.toInt(),
		.blockIndex = backend.blockIndices[index],
		.bidiLevel = uchar(si.analysis.bidiLevel),
		.object = (si.analysis.flags >= QScriptAnalysis::TabOrObject),
		.newline = (si.analysis.flags
			== QScriptAnalysis::LineOrParagraphSeparator),
	};
}

CharAttribute LineShaper::attribute(int position) const {
	Expects(backend().attributes != nullptr);

	const auto &attribute = backend().attributes[position];
	return {
		.graphemeBoundary = bool(attribute.graphemeBoundary),
		.lineBreak = bool(attribute.lineBreak),
		.whiteSpace = bool(attribute.whiteSpace),
	};
}

gsl::span<const int> LineShaper::visualOrder() const {
	return backend().visualOrder;
}

int LineShaper::findItem(int position) const {
	return backend().engine.wrapped().findItem(position);
}

void LineShaper::shapeRange(
		int firstItem,
		int lastItem,
		bool withAttributes) {
	auto &backend = this->backend();
	auto &e = backend.engine.wrapped();
	const auto count = lastItem - firstItem + 1;
	backend.first = firstItem;
	backend.count = count;
	backend.fontEngines.resize(count);
	backend.blockIndices.resize(count);
	if (withAttributes) {
		// Before shaping: asked for after it, they take the room twice
		e.attributes();
	}

	auto levels = QVarLengthArray<uchar>(count);
	auto skipIndex = -1;
	for (auto i = 0; i != count; ++i) {
		const auto blockIt = backend.engine.shapeGetBlock(firstItem + i);
		auto &si = e.layoutData->items[firstItem + i];
		backend.blockIndices[i] = backend.engine.blockIndex(si.position);
		if ((*blockIt)->type() == TextBlockType::Skip) {
			// A skip is laid out as if it had no direction of its own, so
			// that it stays at the end of the line it trails.
			levels[i] = si.analysis.bidiLevel = 0;
			skipIndex = i;
		} else {
			levels[i] = si.analysis.bidiLevel;
		}
		if (si.analysis.flags < QScriptAnalysis::TabOrObject) {
			// Taken now, while shaping this item has left its block's font
			// on the engine: after the loop only the last one of them is.
			backend.fontEngines[i] = e.fontEngine(si);
		}
	}
	if (withAttributes) {
		backend.attributes = e.attributes();
	}

	auto &visualOrder = backend.visualOrder;
	visualOrder.resize(count);
	QTextEngine::bidiReorder(count, levels.data(), visualOrder.data());
	if (style::RightToLeft() && skipIndex == count - 1) {
		for (auto i = count; i > 1;) {
			--i;
			visualOrder[i] = visualOrder[i - 1];
		}
		visualOrder[0] = skipIndex;
	}
}

ShapedItem LineShaper::shape(int index) {
	static_assert(sizeof(ItemView) <= sizeof(ShapedItem::_view));
	static_assert(alignof(ItemView) <= alignof(std::max_align_t));
	static_assert(std::is_trivially_copyable_v<ItemView>);
	const auto view = backend().view(index);
	return ShapedItem(
		this,
		index,
		view.length,
		(view.si->analysis.bidiLevel % 2) != 0,
		bytes::object_as_span(&view));
}

int LineShaper::blockIndex(int position) const {
	return backend().engine.blockIndex(position);
}

QString FontPath(const QString &path) {
	return path;
}

} // namespace Ui::Text
