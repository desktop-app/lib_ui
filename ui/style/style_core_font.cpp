// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_font.h"

#include "base/algorithm.h"
#include "ui/ui_log.h"

#include <QtCore/QMap>
#include <QtCore/QVector>
#include <QtGui/QFontInfo>
#include <QtGui/QFontDatabase>

void style_InitFontsResource() {
#ifndef DESKTOP_APP_USE_PACKAGED_FONTS
	Q_INIT_RESOURCE(fonts);
#endif // !DESKTOP_APP_USE_PACKAGED_FONTS
#ifdef Q_OS_WIN
	Q_INIT_RESOURCE(win);
#elif defined Q_OS_MAC // Q_OS_WIN
	Q_INIT_RESOURCE(mac);
#elif defined Q_OS_LINUX && !defined DESKTOP_APP_USE_PACKAGED // Q_OS_WIN || Q_OS_MAC
	Q_INIT_RESOURCE(linux);
#endif // Q_OS_WIN || Q_OS_MAC || (Q_OS_LINUX && !DESKTOP_APP_USE_PACKAGED)
}

namespace style {
namespace internal {
namespace {

QMap<QString, int> fontFamilyMap;
QVector<QString> fontFamilies;
QMap<uint32, FontData*> fontsMap;

uint32 fontKey(int size, uint32 flags, int family) {
	return (((uint32(family) << 10) | uint32(size)) << 4) | flags;
}

bool ValidateFont(const QString &familyName, int flags = 0) {
	QFont checkFont(familyName);
	checkFont.setPixelSize(13);
	checkFont.setBold(flags & style::internal::FontBold);
	checkFont.setItalic(flags & style::internal::FontItalic);
	checkFont.setUnderline(flags & style::internal::FontUnderline);
	checkFont.setStyleStrategy(QFont::PreferQuality);
	auto realFamily = QFontInfo(checkFont).family();
	if (realFamily.trimmed().compare(familyName, Qt::CaseInsensitive)) {
		UI_LOG(("Font Error: could not resolve '%1' font, got '%2'.").arg(familyName).arg(realFamily));
		return false;
	}

	auto metrics = QFontMetrics(checkFont);
	if (!metrics.height()) {
		UI_LOG(("Font Error: got a zero height in '%1'.").arg(familyName));
		return false;
	}

	return true;
}

bool LoadCustomFont(const QString &filePath, const QString &familyName, int flags = 0) {
	auto regularId = QFontDatabase::addApplicationFont(filePath);
	if (regularId < 0) {
		UI_LOG(("Font Error: could not add '%1'.").arg(filePath));
		return false;
	}

	const auto found = [&] {
		for (auto &family : QFontDatabase::applicationFontFamilies(regularId)) {
			UI_LOG(("Font: from '%1' loaded '%2'").arg(filePath).arg(family));
			if (!family.trimmed().compare(familyName, Qt::CaseInsensitive)) {
				return true;
			}
		}
		return false;
	}();
	if (!found) {
		UI_LOG(("Font Error: could not locate '%1' font in '%2'.").arg(familyName).arg(filePath));
		return false;
	}

	return ValidateFont(familyName, flags);
}

enum {
	FontTypeRegular = 0,
	FontTypeRegularItalic,
	FontTypeBold,
	FontTypeBoldItalic,
	FontTypeSemibold,
	FontTypeSemiboldItalic,

	FontTypesCount,
};
#ifndef DESKTOP_APP_USE_PACKAGED_FONTS
QString FontTypeNames[FontTypesCount] = {
	"DAOpenSansRegular",
	"DAOpenSansRegularItalic",
	"DAOpenSansBold",
	"DAOpenSansBoldItalic",
	"DAOpenSansSemibold",
	"DAOpenSansSemiboldItalic",
};
#endif // !DESKTOP_APP_USE_PACKAGED_FONTS
int32 FontTypeFlags[FontTypesCount] = {
	0,
	FontItalic,
	FontBold,
	FontBold | FontItalic,
	0,
	FontItalic,
};
#ifdef Q_OS_WIN
QString FontTypeWindowsFallback[FontTypesCount] = {
	"Segoe UI",
	"Segoe UI",
	"Segoe UI",
	"Segoe UI",
	"Segoe UI Semibold",
	"Segoe UI Semibold",
};
#endif // Q_OS_WIN

bool Started = false;
QString Overrides[FontTypesCount];

} // namespace

void StartFonts() {
	if (Started) {
		return;
	}
	Started = true;

	style_InitFontsResource();

#ifndef DESKTOP_APP_USE_PACKAGED_FONTS
	bool areGood[FontTypesCount] = { false };
	for (auto i = 0; i != FontTypesCount; ++i) {
		const auto name = FontTypeNames[i];
		const auto flags = FontTypeFlags[i];
		areGood[i] = LoadCustomFont(":/gui/fonts/" + name + ".ttf", name, flags);
		Overrides[i] = name;
#ifdef Q_OS_WIN
		// Attempt to workaround a strange font bug with Open Sans Semibold not loading.
		// See https://github.com/telegramdesktop/tdesktop/issues/3276 for details.
		// Crash happens on "options.maxh / _t->_st->font->height" with "division by zero".
		// In that place "_t->_st->font" is "semiboldFont" is "font(13 "Open Sans Semibold").
		const auto fallback = FontTypeWindowsFallback[i];
		if (!areGood[i]) {
			if (ValidateFont(fallback, flags)) {
				Overrides[i] = fallback;
				UI_LOG(("Fonts Info: Using '%1' instead of '%2'.").arg(fallback).arg(name));
			}
		}
		// Disable default fallbacks to Segoe UI, see:
		// https://github.com/telegramdesktop/tdesktop/issues/5368
		//
		//QFont::insertSubstitution(name, fallback);
#endif // Q_OS_WIN
	}
#endif // !DESKTOP_APP_USE_PACKAGED_FONTS
#ifdef Q_OS_MAC
	auto list = QStringList();
	list.append(".SF NS Text");
	list.append("Helvetica Neue");
	list.append("Lucida Grande");
	for (const auto &name : FontTypeNames) {
		QFont::insertSubstitutions(name, list);
	}
#endif // Q_OS_MAC
}

QString GetPossibleEmptyOverride(const QString &familyName, int32 flags) {
	flags = flags & (FontBold | FontItalic);
	if (familyName == qstr("Open Sans")) {
		if (flags == (FontBold | FontItalic)) {
			return Overrides[FontTypeBoldItalic];
		} else if (flags == FontBold) {
			return Overrides[FontTypeBold];
		} else if (flags == FontItalic) {
			return Overrides[FontTypeRegularItalic];
		} else if (flags == 0) {
			return Overrides[FontTypeRegular];
		}
	} else if (familyName == qstr("Open Sans Semibold")) {
		if (flags == FontItalic) {
			return Overrides[FontTypeSemiboldItalic];
		} else if (flags == 0) {
			return Overrides[FontTypeSemibold];
		}
	}
	return QString();
}

QString GetFontOverride(const QString &familyName, int32 flags) {
	const auto result = GetPossibleEmptyOverride(familyName, flags);
	return result.isEmpty() ? familyName : result;
}

void destroyFonts() {
	for (auto fontData : fontsMap) {
		delete fontData;
	}
	fontsMap.clear();
}

int registerFontFamily(const QString &family) {
	auto result = fontFamilyMap.value(family, -1);
	if (result < 0) {
		result = fontFamilies.size();
		fontFamilyMap.insert(family, result);
		fontFamilies.push_back(family);
	}
	return result;
}

FontData::FontData(int size, uint32 flags, int family, Font *other)
: f(GetFontOverride(fontFamilies[family], flags))
, m(f)
, _size(size)
, _flags(flags)
, _family(family) {
	if (other) {
		memcpy(modified, other, sizeof(modified));
	} else {
		memset(modified, 0, sizeof(modified));
	}
	modified[_flags] = Font(this);

	f.setPixelSize(size);
	if (_flags & FontBold) {
		f.setBold(true);
	//} else if (fontFamilies[family] == "Open Sans Semibold") {
	//	f.setWeight(QFont::DemiBold);
	}
	f.setItalic(_flags & FontItalic);
	f.setUnderline(_flags & FontUnderline);
	f.setStrikeOut(_flags & FontStrikeOut);
	f.setStyleStrategy(QFont::PreferQuality);

	m = QFontMetrics(f);
	height = m.height();
	ascent = m.ascent();
	descent = m.descent();
	spacew = width(QLatin1Char(' '));
	elidew = width("...");
}

Font FontData::bold(bool set) const {
	return otherFlagsFont(FontBold, set);
}

Font FontData::italic(bool set) const {
	return otherFlagsFont(FontItalic, set);
}

Font FontData::underline(bool set) const {
	return otherFlagsFont(FontUnderline, set);
}

Font FontData::strikeout(bool set) const {
	return otherFlagsFont(FontStrikeOut, set);
}

int FontData::size() const {
	return _size;
}

uint32 FontData::flags() const {
	return _flags;
}

int FontData::family() const {
	return _family;
}

Font FontData::otherFlagsFont(uint32 flag, bool set) const {
	int32 newFlags = set ? (_flags | flag) : (_flags & ~flag);
	if (!modified[newFlags].v()) {
		modified[newFlags] = Font(_size, newFlags, _family, modified);
	}
	return modified[newFlags];
}

Font::Font(int size, uint32 flags, const QString &family) {
	if (fontFamilyMap.isEmpty()) {
		for (uint32 i = 0, s = fontFamilies.size(); i != s; ++i) {
			fontFamilyMap.insert(fontFamilies.at(i), i);
		}
	}

	auto i = fontFamilyMap.constFind(family);
	if (i == fontFamilyMap.cend()) {
		fontFamilies.push_back(family);
		i = fontFamilyMap.insert(family, fontFamilies.size() - 1);
	}
	init(size, flags, i.value(), 0);
}

Font::Font(int size, uint32 flags, int family) {
	init(size, flags, family, 0);
}

Font::Font(int size, uint32 flags, int family, Font *modified) {
	init(size, flags, family, modified);
}

void Font::init(int size, uint32 flags, int family, Font *modified) {
	uint32 key = fontKey(size, flags, family);
	auto i = fontsMap.constFind(key);
	if (i == fontsMap.cend()) {
		i = fontsMap.insert(key, new FontData(size, flags, family, modified));
	}
	ptr = i.value();
}

} // namespace internal
} // namespace style
