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

struct ResolvedFont {
	ResolvedFont(FontResolveResult result, FontVariants *modified);

	FontResolveResult result;
	FontData data;
};

ResolvedFont::ResolvedFont(FontResolveResult result, FontVariants *modified)
: result(std::move(result))
, data(this->result, modified) {
}

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

base::flat_map<QString, int> FontFamilyIndices;
std::vector<QString> FontFamilies;
base::flat_map<uint32, std::unique_ptr<ResolvedFont>> FontsByKey;
base::flat_map<uint64, uint32> QtFontsKeys;

[[nodiscard]] uint32 FontKey(int size, FontFlags flags, int family) {
	return (uint32(family) << 18)
		| (uint32(size) << 6)
		| uint32(flags.value());
}

[[nodiscard]] uint64 QtFontKey(const QFont &font) {
	static auto Families = base::flat_map<QString, int>();

	const auto pixelSize = font.pixelSize();
	const auto family = font.family();
	auto i = Families.find(family);
	if (i == end(Families)) {
		i = Families.emplace(family, Families.size()).first;
	}
	return (uint64(i->second) << 24)
		| (uint64(font.weight()) << 16)
		| (uint64(font.bold() ? 1 : 0) << 15)
		| (uint64(font.italic() ? 1 : 0) << 14)
		| (uint64(font.underline() ? 1 : 0) << 13)
		| (uint64(font.strikeOut() ? 1 : 0) << 12)
		| (uint64(font.pixelSize()));
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

struct Metrics {
	int pixelSize = 0;
	float64 ascent = 0.;
	float64 height = 0.;
};
[[nodiscard]] Metrics ComputeMetrics(QFont font, bool adjust) {
	constexpr auto kMaxSizeShift = 8;

	const auto startSize = font.pixelSize();
	const auto metrics = QFontMetricsF(font);
	const auto simple = [&] {
		return Metrics{
			.pixelSize = startSize,
			.ascent = metrics.ascent(),
			.height = metrics.height(),
		};
	};

	const auto family = font.family();
	const auto basic = u"Open Sans"_q;
	if (family == basic) {
		return simple();
	}

	auto copy = font;
	copy.setFamily(basic);
	const auto basicMetrics = QFontMetricsF(copy);

	static const auto Full = u"bdfghijklpqtyBDFGHIJKLPQTY1234567890[]{}()"_q;
	static const auto Test = u"acemnorsuvwxz"_q;

	const auto desired = basicMetrics.tightBoundingRect(Test).height();
	const auto desiredFull = basicMetrics.tightBoundingRect(Full);
	if (desired < 1. || desiredFull.height() < desired) {
		return simple();
	}

	const auto adjusted = [&](int size, const QFontMetricsF &metrics) {
		const auto full = metrics.tightBoundingRect(Full);
		const auto desiredTightAscent = -desiredFull.y();
		const auto desiredTightHeight = desiredFull.height();
		const auto ascentAdd = basicMetrics.ascent() - desiredTightAscent;
		const auto heightAdd = basicMetrics.height() - desiredTightHeight;
		const auto tightAscent = -full.y();
		const auto tightHeight = full.height();
		return Metrics{
			.pixelSize = size,
			.ascent = tightAscent + ascentAdd,
			.height = tightHeight + heightAdd,
		};
	};

	auto current = metrics.tightBoundingRect(Test).height();
	if (current < 1.) {
		return simple();
	} else if (std::abs(current - desired) < 0.2) {
		return adjusted(startSize, metrics);
	}

	const auto adjustedByFont = [&](const QFont &font) {
		return adjusted(font.pixelSize(), QFontMetricsF(font));
	};
	const auto max = std::min(kMaxSizeShift, startSize - 1);
	if (current < desired) {
		for (auto i = 0; i != max; ++i) {
			const auto shift = i + 1;
			font.setPixelSize(startSize + shift);
			const auto metrics = QFontMetricsF(font);
			const auto now = metrics.tightBoundingRect(Test).height();
			if (now > desired) {
				const auto better = (now - desired) < (desired - current);
				if (better) {
					return adjusted(startSize + shift, metrics);
				}
				font.setPixelSize(startSize + shift - 1);
				return adjustedByFont(font);
			}
			current = now;
		}
		font.setPixelSize(startSize + max);
		return adjustedByFont(font);
	} else {
		for (auto i = 0; i != max; ++i) {
			const auto shift = i + 1;
			font.setPixelSize(startSize - shift);
			const auto metrics = QFontMetricsF(font);
			const auto now = metrics.tightBoundingRect(Test).height();
			if (now < desired) {
				const auto better = (desired - now) < (current - desired);
				if (better) {
					return adjusted(startSize - shift, metrics);
				}
				font.setPixelSize(startSize - shift + 1);
				return adjustedByFont(font);
			}
			current = now;
		}
		font.setPixelSize(startSize - max);
		return adjustedByFont(font);
	}
}

[[nodiscard]] FontResolveResult ResolveFont(
		const QString &family,
		FontFlags flags,
		int size,
		bool skipSizeAdjustment) {
	auto font = QFont();

	const auto monospace = (flags & FontFlag::Monospace) != 0;
	const auto system = !monospace && (family == SystemFontTag());
	const auto overriden = !monospace && !system && !family.isEmpty();
	if (monospace) {
		font.setFamily(MonospaceFont());
	} else if (system) {
	} else if (overriden) {
		font.setFamily(family);
	} else {
		font.setFamily("Open Sans"_q);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
		font.setFeature("ss03", true);
#endif // Qt >= 6.7.0
	}
	font.setPixelSize(size);

	font.setWeight((flags & (FontFlag::Bold | FontFlag::Semibold))
		? QFont::DemiBold
		: QFont::Normal);
	if (font.bold()) {
		const auto style = QFontInfo(font).styleName();
		if (!style.isEmpty() && !style.startsWith(
				"Semibold",
				Qt::CaseInsensitive)) {
			font.setBold(true);
		}
	}

	font.setItalic(flags & FontFlag::Italic);
	font.setUnderline(flags & FontFlag::Underline);
	font.setStrikeOut(flags & FontFlag::StrikeOut);

	const auto adjust = (overriden && !skipSizeAdjustment);
	const auto metrics = ComputeMetrics(font, adjust);
	font.setPixelSize(metrics.pixelSize);

	return {
		.font = font,
		.ascent = metrics.ascent,
		.height = metrics.height,
		.iascent = int(base::SafeRound(metrics.ascent)),
		.iheight = int(base::SafeRound(metrics.height)),
		.requestedFamily = RegisterFontFamily(family),
		.requestedSize = size,
		.requestedFlags = flags,
	};
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

void DestroyFonts() {
	base::take(FontsByKey);
}

int RegisterFontFamily(const QString &family) {
	auto i = FontFamilyIndices.find(family);
	if (i == end(FontFamilyIndices)) {
		i = FontFamilyIndices.emplace(family, FontFamilies.size()).first;
		FontFamilies.push_back(family);
	}
	return i->second;
}

FontData::FontData(const FontResolveResult &result, FontVariants *modified)
: f(result.font)
, _m(f)
, _size(result.requestedSize)
, _family(result.requestedFamily)
, _flags(result.requestedFlags) {
	if (modified) {
		memcpy(&_modified, modified, sizeof(_modified));
	}
	_modified[int(_flags)] = Font(this);

	height = int(base::SafeRound(result.height));
	ascent = int(base::SafeRound(result.ascent));
	descent = height - ascent;
	spacew = width(QLatin1Char(' '));
	elidew = width(u"..."_q);
}

Font FontData::bold(bool set) const {
	return otherFlagsFont(FontFlag::Bold, set);
}

Font FontData::italic(bool set) const {
	return otherFlagsFont(FontFlag::Italic, set);
}

Font FontData::underline(bool set) const {
	return otherFlagsFont(FontFlag::Underline, set);
}

Font FontData::strikeout(bool set) const {
	return otherFlagsFont(FontFlag::StrikeOut, set);
}

Font FontData::semibold(bool set) const {
	return otherFlagsFont(FontFlag::Semibold, set);
}

Font FontData::monospace(bool set) const {
	return otherFlagsFont(FontFlag::Monospace, set);
}

int FontData::size() const {
	return _size;
}

FontFlags FontData::flags() const {
	return _flags;
}

int FontData::family() const {
	return _family;
}

Font FontData::otherFlagsFont(FontFlag flag, bool set) const {
	const auto newFlags = set ? (_flags | flag) : (_flags & ~flag);
	if (!_modified[newFlags]) {
		_modified[newFlags] = Font(_size, newFlags, _family, &_modified);
	}
	return _modified[newFlags];
}

Font::Font(int size, FontFlags flags, const QString &family) {
	init(size, flags, RegisterFontFamily(family), 0);
}

Font::Font(int size, FontFlags flags, int family) {
	init(size, flags, family, 0);
}

Font::Font(int size, FontFlags flags, int family, FontVariants *modified) {
	init(size, flags, family, modified);
}

void Font::init(
		int size,
		FontFlags flags,
		int family,
		FontVariants *modified) {
	const auto key = FontKey(size, flags, family);
	auto i = FontsByKey.find(key);
	if (i == end(FontsByKey)) {
		i = FontsByKey.emplace(
			key,
			std::make_unique<ResolvedFont>(
				ResolveFont(
					family ? FontFamilies[family] : Custom,
					flags,
					size,
					family != 0),
				modified)).first;
		QtFontsKeys.emplace(QtFontKey(i->second->data.f), key);
	}
	_data = &i->second->data;
}

OwnedFont::OwnedFont(const QString &custom, FontFlags flags, int size)
: _data(ResolveFont(custom, flags, size, false), nullptr) {
	_font._data = &_data;
}

} // namespace internal

const FontResolveResult *FindAdjustResult(const QFont &font) {
	const auto key = internal::QtFontKey(font);
	const auto i = internal::QtFontsKeys.find(key);
	return (i != end(internal::QtFontsKeys))
		? &internal::FontsByKey[i->second]->result
		: nullptr;
}

} // namespace style
