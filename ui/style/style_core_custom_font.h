// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace style {

struct CustomFont {
	QString family;
	QString style;
};

inline bool operator==(const CustomFont &a, const CustomFont &b) {
	return (a.family == b.family) && (a.style == b.style);
}

inline bool operator<(const CustomFont &a, const CustomFont &b) {
	return (a.family < b.family)
		|| (a.family == b.family && a.style < b.style);
}

inline bool operator!=(const CustomFont &a, const CustomFont &b) {
	return !(a == b);
}

inline bool operator>(const CustomFont &a, const CustomFont &b) {
	return (b < a);
}

inline bool operator<=(const CustomFont &a, const CustomFont &b) {
	return !(b < a);
}

inline bool operator>=(const CustomFont &a, const CustomFont &b) {
	return !(a < b);
}

void SetCustomFonts(const CustomFont &regular, const CustomFont &bold);

[[nodiscard]] QFont ResolveFont(uint32 flags, int size);

} // namespace style
