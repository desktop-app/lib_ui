// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"
#include "base/flags.h"

#include <QtGui/QFont>
#include <QtGui/QFontMetrics>

#include <cmath>

namespace style {

[[nodiscard]] const QString &SystemFontTag();
void SetCustomFont(const QString &font);

enum class FontFlag : uchar {
	Bold = 0x01,
	Italic = 0x02,
	Underline = 0x04,
	StrikeOut = 0x08,
	Semibold = 0x10,
	Monospace = 0x20,
};
inline constexpr bool is_flag_type(FontFlag) { return true; }
using FontFlags = base::flags<FontFlag>;

struct FontResolveResult {
	QFont font;
	float64 ascent = 0.;
	float64 height = 0.;
	int iascent = 0;
	int iheight = 0;
	int requestedFamily = 0;
	int requestedSize = 0;
	FontFlags requestedFlags;
};
[[nodiscard]] const FontResolveResult *FindAdjustResult(const QFont &font);

namespace internal {

void StartFonts();

void DestroyFonts();
int RegisterFontFamily(const QString &family);

inline constexpr auto kFontVariants = 0x40;

class Font;
using FontVariants = std::array<Font, kFontVariants>;

class FontData;
class Font final {
public:
	Font(Qt::Initialization = Qt::Uninitialized) {
	}
	Font(int size, FontFlags flags, const QString &family);
	Font(int size, FontFlags flags, int family);

	[[nodiscard]] FontData *operator->() const {
		return _data;
	}
	[[nodiscard]] FontData *get() const {
		return _data;
	}

	[[nodiscard]] operator bool() const {
		return _data != nullptr;
	}

	[[nodiscard]] operator const QFont &() const;

private:
	friend class FontData;
	friend class OwnedFont;

	FontData *_data = nullptr;

	void init(int size, FontFlags flags, int family, FontVariants *modified);
	friend void StartManager();

	explicit Font(FontData *data) : _data(data) {
	}
	Font(int size, FontFlags flags, int family, FontVariants *modified);

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

	[[nodiscard]] int size() const;
	[[nodiscard]] FontFlags flags() const;
	[[nodiscard]] int family() const;

	QFont f;
	int height = 0;
	int ascent = 0;
	int descent = 0;
	int spacew = 0;
	int elidew = 0;

private:
	friend class OwnedFont;
	friend struct ResolvedFont;

	mutable FontVariants _modified;

	[[nodiscard]] Font otherFlagsFont(FontFlag flag, bool set) const;
	FontData(const FontResolveResult &data, FontVariants *modified);

	QFontMetricsF _m;
	int _size = 0;
	int _family = 0;
	FontFlags _flags = 0;

};

inline bool operator==(const Font &a, const Font &b) {
	return a.get() == b.get();
}
inline bool operator!=(const Font &a, const Font &b) {
	return a.get() != b.get();
}

inline Font::operator const QFont &() const {
	Expects(_data != nullptr);

	return _data->f;
}

class OwnedFont final {
public:
	OwnedFont(const QString &custom, FontFlags flags, int size);
	OwnedFont(const OwnedFont &other)
	: _data(other._data) {
		_font._data = &_data;
	}

	OwnedFont &operator=(const OwnedFont &other) {
		_data = other._data;
		return *this;
	}

	[[nodiscard]] const Font &font() const {
		return _font;
	}
	[[nodiscard]] FontData *operator->() const {
		return _font.get();
	}
	[[nodiscard]] FontData *get() const {
		return _font.get();
	}

private:
	FontData _data;
	Font _font;

};

} // namespace internal
} // namespace style
