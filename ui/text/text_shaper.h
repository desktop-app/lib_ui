// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/ui_fixed.h"
#include "ui/text/text.h"

#include <QtCore/QPointF>
#include <QtGui/QFont>

#include <memory>
#include <vector>

class QPainter;

namespace Ui::Text {

// Everything below is what the renderer needs from a text backend, and nothing
// of a backend shows through: no glyphs, no clusters, no engine types. That is
// what lets the implementation be chosen when the library is built without the
// callers - or anything that includes them - being rebuilt.
struct ShapeEntry;

// One itemized run: a stretch of text with a single font, direction and
// script, or a single object block.
struct Item {
	int position = 0;
	int length = 0;
	Fixed width; // Object items carry AbstractBlock::objectWidth() here.
	int ascent = 0;
	int descent = 0;
	int blockIndex = 0;
	uchar bidiLevel = 0;
	bool object = false;
	bool newline = false;

	[[nodiscard]] bool rtl() const {
		return (bidiLevel % 2) != 0;
	}
};

// Filled by the backend once per line, read in the callers' inner loops.
struct CharAttribute {
	bool graphemeBoundary : 1 = false;
	bool lineBreak : 1 = false;
	bool whiteSpace : 1 = false;
};

// Deliberately glyph-free: the callers ask about character offsets, never
// about clusters or glyph indices, and the backend answers with pixels.
//
// Offsets are relative to the item start, and a "till" offset is one past the
// last character, as everywhere else in the engine.
class ShapedItem final {
public:
	[[nodiscard]] explicit operator bool() const {
		return _entry != nullptr;
	}

	[[nodiscard]] int length() const {
		return _length;
	}
	[[nodiscard]] bool rtl() const {
		return _rtl;
	}

	[[nodiscard]] Fixed width() const;
	[[nodiscard]] Fixed width(int fromOffset, int tillOffset) const;

	// The character a point falls on, and whether it fell in its second half,
	// which is where a caret goes after it rather than before.
	struct Hit {
		int offset = 0;
		bool after = false;
	};
	[[nodiscard]] Hit hitTest(
		Fixed x,
		int fromOffset,
		int tillOffset) const;

	// End of the cluster that starts at the given offset - characters drawn
	// as one glyph can not be cut apart.
	[[nodiscard]] int clusterEnd(int offset, int tillOffset) const;

	// The same, but never before a place a caret could go, which is where
	// a line may be cut for an ellipsis.
	[[nodiscard]] int cutEnd(int offset, int tillOffset) const;

	// A character the shaper folded away, drawing nothing for it, which is
	// what happens to a space that ends up at a cut.
	[[nodiscard]] bool invisibleAt(int offset) const;

	// How much a glyph sticks out to the left of where the next one starts,
	// as a non-positive value, or zero when nothing is known.
	[[nodiscard]] Fixed rightBearingBefore(int offset) const;

	// The font is passed in because the caller knows better than the shaping
	// did - a link is underlined only while the cursor is over it.
	void draw(
		QPainter &p,
		QPointF at,
		int fromOffset,
		int tillOffset,
		const QFont &font) const;

private:
	friend class LineShaper;

	const ShapeEntry *_entry = nullptr;
	int _length = 0;
	bool _rtl = false;

};

// Direction resolution for one paragraph, kept out of the lines because the
// answer for a character depends on the whole paragraph around it, and because
// the lines of a paragraph are laid out one after another and would otherwise
// redo it.
class Paragraph final {
public:
	Paragraph();
	Paragraph(Paragraph &&other);
	Paragraph &operator=(Paragraph &&other);
	~Paragraph();

	void clear();
	[[nodiscard]] bool ready() const;
	void resolve(
		not_null<const String*> t,
		int position,
		int length,
		bool baseRtl,
		int blockIndexHint,
		int blockIndexLimit);

	// The ellipsis is not a part of the text, so it is laid out as a tail of
	// the paragraph that goes the same way as the character before it.
	void elide(int position, int count, bool baseRtl);

private:
	friend class LineShaper;

	struct State;
	std::unique_ptr<State> _state;

};

// One line of text, itemized and shaped by whichever backend the library was
// built with. Created per line, which is what the callers already do.
class LineShaper final {
public:
	LineShaper(
		not_null<const String*> t,
		Paragraph &paragraph,
		int offset,
		const QString &text,
		int blockIndexHint = 0,
		int blockIndexLimit = -1);
	~LineShaper();

	// The item a position falls in, or -1. The line is shaped with some text
	// around it, so not every item of it is drawn.
	[[nodiscard]] int findItem(int position) const;

	// Shapes the items that are drawn, which the renderer needs done before
	// it can place any of them. Everything below is about these items only,
	// and indexes them from zero.
	void shapeRange(int firstItem, int lastItem);

	[[nodiscard]] const std::vector<Item> &items() const;

	// Grapheme, line break and space flags for the whole line, filled once.
	[[nodiscard]] gsl::span<const CharAttribute> attributes() const;

	// Items in the order they are drawn, which is not the order they are in
	// when the line mixes directions.
	[[nodiscard]] gsl::span<const int> visualOrder() const;

	[[nodiscard]] int blockIndex(int position) const;

	[[nodiscard]] const ShapedItem &shape(int item);

private:
	[[nodiscard]] int itemLength(int index) const;

	struct Backend;
	std::unique_ptr<Backend> _backend;

};

} // namespace Ui::Text
