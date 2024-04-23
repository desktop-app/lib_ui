// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

#include <QtGui/QFont>
#include <QtGui/QFontMetrics>

#include <cmath>

namespace style {

[[nodiscard]] const QString &SystemFontTag();
void SetCustomFont(const QString &font);

[[nodiscard]] QFont ResolveFont(
	const QString &custom,
	uint32 flags,
	int size);

namespace internal {

void StartFonts();
[[nodiscard]] QString GetFontOverride(int32 flags = 0);

void destroyFonts();
int registerFontFamily(const QString &family);

class FontData;
class Font {
public:
	Font(Qt::Initialization = Qt::Uninitialized) {
	}
	Font(int size, uint32 flags, const QString &family);
	Font(int size, uint32 flags, int family);

	FontData *operator->() const {
		return ptr;
	}
	FontData *v() const {
		return ptr;
	}

	operator bool() const {
		return !!ptr;
	}

	operator const QFont &() const;

private:
	FontData *ptr = nullptr;

	void init(int size, uint32 flags, int family, Font *modified);
	friend void startManager();

	Font(FontData *p) : ptr(p) {
	}
	Font(int size, uint32 flags, int family, Font *modified);
	friend class FontData;

};

enum FontFlags {
	FontBold = 0x01,
	FontItalic = 0x02,
	FontUnderline = 0x04,
	FontStrikeOut = 0x08,
	FontSemibold = 0x10,
	FontMonospace = 0x20,

	FontDifferentFlags = 0x40,
};

class FontData {
public:
	[[nodiscard]] int width(const QString &text) const {
		return int(std::ceil(_m.horizontalAdvance(text)));
	}
	[[nodiscard]] int width(const QString &text, int from, int to) const {
		return width(text.mid(from, to));
	}
	[[nodiscard]] int width(QChar ch) const {
		return int(std::ceil(_m.horizontalAdvance(ch)));
	}
	[[nodiscard]] QString elided(
			const QString &str,
			int width,
			Qt::TextElideMode mode = Qt::ElideRight) const {
		return _m.elidedText(str, mode, width);
	}

	[[nodiscard]] Font bold(bool set = true) const;
	[[nodiscard]] Font italic(bool set = true) const;
	[[nodiscard]] Font underline(bool set = true) const;
	[[nodiscard]] Font strikeout(bool set = true) const;
	[[nodiscard]] Font semibold(bool set = true) const;
	[[nodiscard]] Font monospace(bool set = true) const;

	int size() const;
	uint32 flags() const;
	int family() const;

	QFont f;
	int32 height, ascent, descent, spacew, elidew;

private:
	mutable Font _modified[FontDifferentFlags];

	Font otherFlagsFont(uint32 flag, bool set) const;
	FontData(int size, uint32 flags, int family, Font *other);

	friend class Font;
	QFontMetricsF _m;
	int _size;
	uint32 _flags;
	int _family;

};

inline bool operator==(const Font &a, const Font &b) {
	return a.v() == b.v();
}
inline bool operator!=(const Font &a, const Font &b) {
	return a.v() != b.v();
}

inline Font::operator const QFont &() const {
	return ptr->f;
}

} // namespace internal
} // namespace style
