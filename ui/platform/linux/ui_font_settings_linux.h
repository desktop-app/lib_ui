// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Ui::Platform {

enum class FontAntialias : uchar {
	None,
	Grayscale,
	Subpixel,
};

enum class FontHinting : uchar {
	None,
	Slight,
	Medium,
	Full,
};

enum class FontSubpixelOrder : uchar {
	Rgb,
	Bgr,
	Vrgb,
	Vbgr,
};

// What the desktop was told about drawing text. Anything it did not say is
// left empty, so that the answer fontconfig matched for a font stays in use.
struct FontRenderSettings {
	std::optional<FontAntialias> antialias;
	std::optional<FontHinting> hinting;
	std::optional<FontSubpixelOrder> subpixelOrder;

	friend inline bool operator==(
		const FontRenderSettings &,
		const FontRenderSettings &) = default;
};

[[nodiscard]] FontRenderSettings FontSettings();

// Fires when the desktop is told something else than it was told before, so
// that the text drawn with the old answer can be drawn again with the new one.
[[nodiscard]] rpl::producer<> FontSettingsChanges();

} // namespace Ui::Platform
