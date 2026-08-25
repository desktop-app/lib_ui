// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/linux/ui_font_settings_linux.h"

#include "base/platform/linux/base_linux_xdp_utilities.h"
#include "base/platform/linux/base_linux_xsettings.h"
#include "base/integration.h"

#include <QtCore/QVariant>

namespace Ui::Platform {
namespace {

// Named the way the settings daemon of GNOME names them, because that is what
// both the portal and XSettings carry - the portal under the schema of the
// desktop, XSettings under the keys Xft has always used. The two spellings of
// a hint style are the same value said twice: "slight" in the schema, and
// "hintslight" in XSettings.
[[nodiscard]] std::optional<FontHinting> ParseHinting(const QString &value) {
	if (value == u"none"_q || value == u"hintnone"_q) {
		return FontHinting::None;
	} else if (value == u"slight"_q || value == u"hintslight"_q) {
		return FontHinting::Slight;
	} else if (value == u"medium"_q || value == u"hintmedium"_q) {
		return FontHinting::Medium;
	} else if (value == u"full"_q || value == u"hintfull"_q) {
		return FontHinting::Full;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<FontSubpixelOrder> ParseSubpixelOrder(
		const QString &value) {
	if (value == u"rgb"_q) {
		return FontSubpixelOrder::Rgb;
	} else if (value == u"bgr"_q) {
		return FontSubpixelOrder::Bgr;
	} else if (value == u"vrgb"_q) {
		return FontSubpixelOrder::Vrgb;
	} else if (value == u"vbgr"_q) {
		return FontSubpixelOrder::Vbgr;
	}
	return std::nullopt;
}

// Said by the schema of the desktop as one word: no antialiasing at all, grey
// antialiasing, or antialiasing over the subpixels - and the order of those
// subpixels is a key of its own, which only means anything for the last.
[[nodiscard]] std::optional<FontAntialias> ParseAntialias(
		const QString &value) {
	if (value == u"none"_q) {
		return FontAntialias::None;
	} else if (value == u"grayscale"_q) {
		return FontAntialias::Grayscale;
	} else if (value == u"rgba"_q) {
		return FontAntialias::Subpixel;
	}
	return std::nullopt;
}

// Taken one at a time, because a place that is told about the antialiasing is
// not necessarily told about the hinting - and what it left unsaid has to be
// asked of the next one, not dropped along with everything else.
void Fill(FontRenderSettings &result, Fn<FontRenderSettings()> next) {
	if (result.antialias && result.hinting && result.subpixelOrder) {
		return;
	}
	const auto other = next();
	if (!result.antialias) {
		result.antialias = other.antialias;
	}
	if (!result.hinting) {
		result.hinting = other.hinting;
	}
	if (!result.subpixelOrder) {
		result.subpixelOrder = other.subpixelOrder;
	}
}

// Said by XSettings in two keys instead of one: whether to antialias at all,
// and an order of subpixels that is "none" where the antialiasing is grey.
[[nodiscard]] FontRenderSettings FromXSettings() {
	using base::Platform::XCB::XSettings;
	const auto xSettings = XSettings::Instance();
	if (!xSettings) {
		return {};
	}
	const auto string = [&](const char *name) {
		const auto value = xSettings->setting(name);
		return value.isValid() ? value.toString() : QString();
	};
	const auto number = [&](const char *name) {
		const auto value = xSettings->setting(name);
		return value.isValid()
			? std::make_optional(value.toInt())
			: std::nullopt;
	};

	auto result = FontRenderSettings();
	const auto order = ParseSubpixelOrder(string("Xft/RGBA"));
	if (const auto antialias = number("Xft/Antialias")) {
		if (*antialias == 0) {
			result.antialias = FontAntialias::None;
		} else if (*antialias > 0) {
			result.antialias = order
				? FontAntialias::Subpixel
				: FontAntialias::Grayscale;
			result.subpixelOrder = order;
		}
	}
	if (const auto hinting = number("Xft/Hinting")) {
		result.hinting = (*hinting == 0)
			? std::make_optional(FontHinting::None)
			: ParseHinting(string("Xft/HintStyle"));
	}
	return result;
}

// Said by the portal, which is the only place it is said inside a sandbox: the
// fontconfig there belongs to the sandbox and knows nothing of the system. The
// schema of the desktop is asked first and the one of the settings daemon
// after it, the way GTK asks - update_xft_settings() in gdksettings-wayland.c.
[[nodiscard]] FontRenderSettings FromPortal() {
	const auto string = [](const char *group, const char *key) {
		auto value = base::Platform::XDP::ReadSetting(group, key);
		return value.has_value()
			? QString::fromStdString(value->get_string(nullptr))
			: QString();
	};
	const auto read = [&](const char *group,
			const char *antialias,
			const char *hinting,
			const char *order) {
		auto result = FontRenderSettings();
		result.antialias = ParseAntialias(string(group, antialias));
		result.hinting = ParseHinting(string(group, hinting));
		result.subpixelOrder = ParseSubpixelOrder(string(group, order));
		return result;
	};

	auto result = read(
		"org.gnome.desktop.interface",
		"font-antialiasing",
		"font-hinting",
		"font-rgba-order");
	Fill(result, [&] {
		return read(
			"org.gnome.settings-daemon.plugins.xsettings",
			"antialiasing",
			"hinting",
			"rgba-order");
	});
	return result;
}

// Watched in both places the answer is read from, because which of them
// answers is not known until it is asked: a session with an XSettings manager
// is told there, one without it is told over the portal.
struct Watch {
	FontRenderSettings last;
	rpl::event_stream<> changes;
	rpl::lifetime lifetime;
	std::optional<base::Platform::XDP::SettingWatcher> watcher;
};

[[nodiscard]] bool IsFontKey(const std::string &group, const std::string &key) {
	return ((group == "org.gnome.desktop.interface")
		&& (key == "font-antialiasing"
			|| key == "font-hinting"
			|| key == "font-rgba-order"))
		|| ((group == "org.gnome.settings-daemon.plugins.xsettings")
			&& (key == "antialiasing"
				|| key == "hinting"
				|| key == "rgba-order"));
}

} // namespace

FontRenderSettings FontSettings() {
	auto result = FromXSettings();
	Fill(result, FromPortal);
	return result;
}

rpl::producer<> FontSettingsChanges() {
	static const auto watch = [] {
		auto result = std::make_unique<Watch>();
		result->last = FontSettings();
		const auto raw = result.get();
		const auto changed = [=] {
			base::Integration::Instance().enterFromEventLoop([=] {
				auto now = FontSettings();
				if (now != raw->last) {
					raw->last = now;
					raw->changes.fire({});
				}
			});
		};
		using base::Platform::XCB::XSettings;
		if (const auto xSettings = XSettings::Instance()) {
			for (const auto name : {
				"Xft/Antialias",
				"Xft/Hinting",
				"Xft/HintStyle",
				"Xft/RGBA",
			}) {
				result->lifetime.add(xSettings->registerCallbackForProperty(
					name,
					[=](xcb_connection_t*, const QByteArray&, const QVariant&) {
						changed();
					}));
			}
		}
		result->watcher.emplace([=](
				const std::string &group,
				const std::string &key,
				gi::repository::GLib::Variant) {
			if (IsFontKey(group, key)) {
				changed();
			}
		});
		return result;
	}();
	return watch->changes.events();
}

} // namespace Ui::Platform
