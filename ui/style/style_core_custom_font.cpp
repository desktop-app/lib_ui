// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_custom_font.h"

#include "ui/style/style_core_font.h"

#include <QGuiApplication>
#include <QFontDatabase>

namespace style {
namespace {

using namespace internal;

auto RegularFont = CustomFont();
auto BoldFont = CustomFont();

} // namespace

void SetCustomFonts(const CustomFont &regular, const CustomFont &bold) {
	RegularFont = regular;
	BoldFont = bold;
}

QFont ResolveFont(const QString &familyOverride, uint32 flags, int size) {
	static auto Database = QFontDatabase();

	const auto bold = ((flags & FontBold) || (flags & FontSemibold));
	const auto italic = (flags & FontItalic);
	const auto &custom = bold ? BoldFont : RegularFont;
	const auto useCustom = !custom.family.isEmpty();

	auto result = QFont(QGuiApplication::font().family());
	if (!familyOverride.isEmpty()) {
		result.setFamily(familyOverride);
		if (bold) {
			result.setBold(true);
		}
	} else if (flags & FontMonospace) {
		result.setFamily(MonospaceFont());
	} else if (useCustom) {
		const auto sizes = Database.smoothSizes(custom.family, custom.style);
		const auto good = sizes.isEmpty()
			? Database.pointSizes(custom.family, custom.style)
			: sizes;
		const auto point = good.isEmpty() ? size : good.front();
		result = Database.font(custom.family, custom.style, point);
	} else {
		result.setFamily(GetFontOverride(flags));
		if (bold) {
#ifdef DESKTOP_APP_USE_PACKAGED_FONTS
			result.setWeight(QFont::DemiBold);
#else // DESKTOP_APP_USE_PACKAGED_FONTS
			result.setBold(true);
#endif // !DESKTOP_APP_USE_PACKAGED_FONTS

			if (flags & FontItalic) {
				result.setStyleName("Semibold Italic");
			} else {
				result.setStyleName("Semibold");
			}
		}
	}
	if (italic) {
		result.setItalic(true);
	}

	result.setUnderline(flags & FontUnderline);
	result.setStrikeOut(flags & FontStrikeOut);
	result.setPixelSize(size);

	return result;
}

} // namespace style
