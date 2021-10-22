// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_color.h"

#include "ui/style/style_core_palette.h"

namespace style {
namespace internal {

Color::Proxy Color::operator[](const style::palette &paletteOverride) const {
	const auto index = main_palette::indexOfColor(*this);
	return { (index >= 0) ? paletteOverride.colorAtIndex(index) : (*this) };
}

ColorData::ColorData(uchar r, uchar g, uchar b, uchar a)
: c(int(r), int(g), int(b), int(a))
, p(c)
, b(c) {
}

void ColorData::set(uchar r, uchar g, uchar b, uchar a) {
	this->c = QColor(int(r), int(g), int(b), int(a));
	this->p = QPen(c);
	this->b = QBrush(c);
}

void ComplexColor::subscribeToPaletteChanges() {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		refresh();
	}, _lifetime);
}

} // namespace internal
} // namespace style
