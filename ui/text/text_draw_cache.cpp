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
}

bool DrawCache::empty() const {
	return _lines.empty();
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

} // namespace Ui::Text
