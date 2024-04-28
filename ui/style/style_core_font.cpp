// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_font.h"

#include "base/algorithm.h"
#include "base/debug_log.h"
#include "base/variant.h"
#include "base/base_file_utilities.h"
#include "ui/integration.h"

#include <QtCore/QMap>
#include <QtCore/QVector>
#include <QtGui/QFontInfo>
#include <QtGui/QFontDatabase>
#include <QtWidgets/QApplication>

void style_InitFontsResource() {
#ifdef Q_OS_MAC // Use resources from the .app bundle on macOS.

	base::RegisterBundledResources(u"lib_ui.rcc"_q);

#else // Q_OS_MAC

#ifndef LIB_UI_USE_PACKAGED_FONTS
	Q_INIT_RESOURCE(fonts);
#endif // !LIB_UI_USE_PACKAGED_FONTS
#ifdef Q_OS_WIN
	Q_INIT_RESOURCE(win);
#endif // Q_OS_WIN

#endif // Q_OS_MAC
}

namespace style {
namespace {

QString Custom;

} // namespace

const QString &SystemFontTag() {
	static const auto result = u"(system)"_q + QChar(0);
	return result;
}

void SetCustomFont(const QString &font) {
	Custom = font;
}

namespace internal {
namespace {

#ifndef LIB_UI_USE_PACKAGED_FONTS
const auto FontTypes = std::array{
	u"OpenSans-Regular"_q,
	u"OpenSans-Italic"_q,
	u"OpenSans-SemiBold"_q,
	u"OpenSans-SemiBoldItalic"_q,
};
const auto PersianFontTypes = std::array{
	u"Vazirmatn-UI-NL-Regular"_q,
	u"Vazirmatn-UI-NL-SemiBold"_q,
};
#endif // !LIB_UI_USE_PACKAGED_FONTS

bool Started = false;

QMap<QString, int> fontFamilyMap;
QVector<QString> fontFamilies;
QMap<uint32, FontData*> fontsMap;

uint32 fontKey(int size, uint32 flags, int family) {
	return (((uint32(family) << 12) | uint32(size)) << 6) | flags;
}

#ifndef LIB_UI_USE_PACKAGED_FONTS
bool LoadCustomFont(const QString &filePath) {
	auto regularId = QFontDatabase::addApplicationFont(filePath);
	if (regularId < 0) {
		LOG(("Font Error: could not add '%1'.").arg(filePath));
		return false;
	}

	for (auto &family : QFontDatabase::applicationFontFamilies(regularId)) {
		LOG(("Font: from '%1' loaded '%2'").arg(filePath, family));
	}

	return true;
}
#endif // !LIB_UI_USE_PACKAGED_FONTS

[[nodiscard]] QString SystemMonospaceFont() {
	const auto type = QFontDatabase::FixedFont;
	return QFontDatabase::systemFont(type).family();
}

[[nodiscard]] QString ManualMonospaceFont() {
	const auto kTryFirst = std::initializer_list<QString>{
		u"Cascadia Mono"_q,
		u"Consolas"_q,
		u"Liberation Mono"_q,
		u"Menlo"_q,
		u"Courier"_q,
	};
	for (const auto &family : kTryFirst) {
		const auto resolved = QFontInfo(QFont(family)).family();
		if (resolved.trimmed().startsWith(family, Qt::CaseInsensitive)) {
			return family;
		}
	}
	return QString();
}

[[nodiscard]] QString MonospaceFont() {
	static const auto family = [&]() -> QString {
		const auto manual = ManualMonospaceFont();
		const auto system = SystemMonospaceFont();

#ifdef Q_OS_WIN
		// Prefer our monospace font.
		const auto useSystem = manual.isEmpty();
#else // Q_OS_WIN
		// Prefer system monospace font.
		const auto metrics = QFontMetrics(QFont(system));
		const auto useSystem = manual.isEmpty()
			|| (metrics.horizontalAdvance(QChar('i')) == metrics.horizontalAdvance(QChar('W')));
#endif // Q_OS_WIN
		return useSystem ? system : manual;
	}();

	return family;
}

[[nodiscard]] int ComputePixelSize(QFont font, uint32 flags, int size) {
	constexpr auto kMaxSizeShift = 6;

	const auto family = font.family();
	const auto basic = u"Open Sans"_q;
	if (family == basic) {
		return size;
	}
	auto copy = font;
	copy.setFamily(basic);
	const auto basicMetrics = QFontMetricsF(copy);
	const auto desiredHeight = basicMetrics.height();
	const auto desiredCap = basicMetrics.capHeight();
	if (desiredHeight < 1. || desiredCap < 1.) {
		return size;
	}
	font.setPixelSize(size);
	const auto currentMetrics = QFontMetricsF(font);
	auto currentHeight = currentMetrics.height();
	auto currentCap = currentMetrics.capHeight();
	const auto max = std::min(kMaxSizeShift, size - 1);
	if (currentHeight < 1.
		|| currentCap < 1.
		|| std::abs(currentCap - desiredCap) < 0.2
		|| std::abs(currentHeight - desiredHeight) < 0.2) {
		return size;
	} else if (currentHeight < desiredHeight) {
		for (auto i = 0; i != max; ++i) {
			const auto shift = i + 1;
			font.setPixelSize(size + shift);
			const auto metrics = QFontMetricsF(font);
			const auto nowHeight = metrics.height();
			const auto nowCap = metrics.capHeight();
			if (nowHeight > desiredHeight || nowCap > desiredCap) {
				const auto heightBetter = (nowHeight - desiredHeight)
					< (desiredHeight - currentHeight);
				const auto capBetter = (nowCap - desiredCap)
					< (desiredCap - currentCap);
				return (heightBetter && capBetter)
					? (size + shift)
					: (size + shift - 1);
			}
			currentHeight = nowHeight;
			currentCap = nowCap;
		}
		return size + kMaxSizeShift;
	} else {
		for (auto i = 0; i != max; ++i) {
			const auto shift = i + 1;
			font.setPixelSize(size - shift);
			const auto metrics = QFontMetricsF(font);
			const auto nowHeight = metrics.height();
			const auto nowCap = metrics.capHeight();
			if (nowHeight < desiredHeight || nowCap < desiredCap) {
				const auto heightBetter = (desiredHeight - nowHeight)
					< (currentHeight - desiredHeight);
				const auto capBetter = (desiredCap - nowCap)
					< (currentCap - desiredCap);
				return (heightBetter && capBetter)
					? (size - shift)
					: (size - shift + 1);
			}
			currentHeight = nowHeight;
			currentCap = nowCap;
		}
		return size - kMaxSizeShift;
	}
}

[[nodiscard]] QFont ResolveFont(
		const QString &family,
		uint32 flags,
		int size,
		bool skipSizeAdjustment) {
	auto result = QFont();
	const auto monospace = (flags & FontMonospace) != 0;
	const auto system = !monospace && (family == SystemFontTag());
	const auto overriden = !monospace && !system && !family.isEmpty();
	if (monospace) {
		result.setFamily(MonospaceFont());
	} else if (system) {
	} else if (overriden) {
		result.setFamily(family);
	} else {
		result.setFamily("Open Sans"_q);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
		result.setFeature("ss03", true);
#endif // Qt >= 6.7.0
	}
	result.setWeight(((flags & FontBold) || (flags & FontSemibold))
		? QFont::DemiBold
		: QFont::Normal);
	if (result.bold()) {
		const auto style = QFontInfo(result).styleName();
		if (!style.isEmpty() && !style.startsWith(
				"Semibold",
				Qt::CaseInsensitive)) {
			result.setBold(true);
		}
	}
	result.setItalic(flags & FontItalic);
	result.setUnderline(flags & FontUnderline);
	result.setStrikeOut(flags & FontStrikeOut);
	result.setPixelSize((monospace || !overriden || skipSizeAdjustment)
		? size
		: ComputePixelSize(result, flags, size));
	return result;
}

} // namespace

void StartFonts() {
	if (Started) {
		return;
	}
	Started = true;

	style_InitFontsResource();

#ifndef LIB_UI_USE_PACKAGED_FONTS
	const auto base = u":/gui/fonts/"_q;
	const auto name = u"Open Sans"_q;

	for (const auto &file : FontTypes) {
		LoadCustomFont(base + file + u".ttf"_q);
	}

	for (const auto &file : PersianFontTypes) {
		LoadCustomFont(base + file + u".ttf"_q);
	}
	QFont::insertSubstitution(name, u"Vazirmatn UI NL"_q);

#ifdef Q_OS_MAC
	const auto list = QStringList{
		u"STIXGeneral"_q,
		u".SF NS Text"_q,
		u"Helvetica Neue"_q,
		u"Lucida Grande"_q,
	};
	QFont::insertSubstitutions(name, list);
#endif // Q_OS_MAC
#endif // !LIB_UI_USE_PACKAGED_FONTS

	auto appFont = QApplication::font();
	appFont.setStyleStrategy(QFont::PreferQuality);
	QApplication::setFont(appFont);
}

void destroyFonts() {
	for (auto fontData : std::as_const(fontsMap)) {
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
: f(ResolveFont(
	family ? fontFamilies[family] : Custom,
	flags,
	size,
	family != 0))
, _m(f)
, _size(size)
, _flags(flags)
, _family(family) {
	if (other) {
		memcpy(_modified, other, sizeof(_modified));
	}
	_modified[_flags] = Font(this);

	height = int(base::SafeRound(_m.height()));
	ascent = int(base::SafeRound(_m.ascent()));
	descent = int(base::SafeRound(_m.descent()));
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

Font FontData::semibold(bool set) const {
	return otherFlagsFont(FontSemibold, set);
}

Font FontData::monospace(bool set) const {
	return otherFlagsFont(FontMonospace, set);
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
	if (!_modified[newFlags].v()) {
		_modified[newFlags] = Font(_size, newFlags, _family, _modified);
	}
	return _modified[newFlags];
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

QFont ResolveFont(const QString &custom, uint32 flags, int size) {
	return internal::ResolveFont(custom, flags, size, false);
}

} // namespace style
