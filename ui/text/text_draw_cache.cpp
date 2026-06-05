// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_draw_cache.h"

namespace Ui::Text {

void DrawCache::clear() {
	_lines.clear();
	_paragraphs.clear();
}

bool DrawCache::empty() const {
	return _lines.empty() && _paragraphs.empty();
}

auto DrawCache::line(int index) -> Line & {
	Expects(index >= 0);

	if (index >= int(_lines.size())) {
		_lines.resize(index + 1);
	}
	return _lines[index];
}

const DrawCache::Line *DrawCache::line(int index) const {
	if (index < 0 || index >= int(_lines.size())) {
		return nullptr;
	}
	return &_lines[index];
}

auto DrawCache::paragraph(
		int start,
		int length,
		int startBlock,
		Qt::LayoutDirection direction) -> Paragraph & {
	for (auto &paragraph : _paragraphs) {
		if (paragraph.start == start
			&& paragraph.length == length
			&& paragraph.startBlock == startBlock
			&& paragraph.direction == direction) {
			return paragraph;
		}
	}
	auto &result = _paragraphs.emplace_back();
	result.start = start;
	result.length = length;
	result.startBlock = startBlock;
	result.direction = direction;
	return result;
}

const DrawCache::Paragraph *DrawCache::paragraph(
		int start,
		int length,
		int startBlock,
		Qt::LayoutDirection direction) const {
	for (const auto &paragraph : _paragraphs) {
		if (paragraph.start == start
			&& paragraph.length == length
			&& paragraph.startBlock == startBlock
			&& paragraph.direction == direction) {
			return &paragraph;
		}
	}
	return nullptr;
}

void SimpleTextCache::clear() {
	data.clear();
	forAvailableWidth = 0;
}

bool SimpleTextCache::empty() const {
	return data.empty();
}

void DrawCached(
		QPainter &p,
		const String &string,
		PaintContext &&context,
		SimpleTextCache &cache) {
	if (cache.forAvailableWidth != context.availableWidth) {
		cache.clear();
		cache.forAvailableWidth = context.availableWidth;
	}
	context.drawCache = &cache.data;
	string.draw(p, context);
}

} // namespace Ui::Text
