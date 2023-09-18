// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/linux/ui_window_title_linux.h"

#include "base/platform/linux/base_linux_xdp_utilities.h"

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xsettings.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <glibmm.h>

namespace Ui {
namespace Platform {
namespace internal {
namespace {

TitleControls::Control GtkKeywordToTitleControl(const QString &keyword) {
	if (keyword == qstr("minimize")) {
		return TitleControls::Control::Minimize;
	} else if (keyword == qstr("maximize")) {
		return TitleControls::Control::Maximize;
	} else if (keyword == qstr("close")) {
		return TitleControls::Control::Close;
	}

	return TitleControls::Control::Unknown;
}

TitleControls::Layout GtkKeywordsToTitleControlsLayout(const QString &keywords) {
	const auto splitted = keywords.split(':');

	std::vector<TitleControls::Control> controlsLeft;
	ranges::transform(
		splitted[0].split(','),
		ranges::back_inserter(controlsLeft),
		GtkKeywordToTitleControl);

	std::vector<TitleControls::Control> controlsRight;
	if (splitted.size() > 1) {
		ranges::transform(
			splitted[1].split(','),
			ranges::back_inserter(controlsRight),
			GtkKeywordToTitleControl);
	}

	return TitleControls::Layout{
		.left = controlsLeft,
		.right = controlsRight,
	};
}

} // namespace

TitleControls::Layout TitleControlsLayout() {
	[[maybe_unused]] static const auto Inited = [] {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
		using base::Platform::XCB::XSettings;
		if (const auto xSettings = XSettings::Instance()) {
			xSettings->registerCallbackForProperty("Gtk/DecorationLayout", [](
					xcb_connection_t *,
					const QByteArray &,
					const QVariant &,
					void *) {
				NotifyTitleControlsLayoutChanged();
			}, nullptr);
		}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

		namespace XDP = base::Platform::XDP;
		static const XDP::SettingWatcher settingWatcher(
			"org.gnome.desktop.wm.preferences",
			"button-layout",
			[] { NotifyTitleControlsLayoutChanged(); });

		return true;
	}();

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	const auto xSettingsResult = []() -> std::optional<TitleControls::Layout> {
		using base::Platform::XCB::XSettings;
		const auto xSettings = XSettings::Instance();
		if (!xSettings) {
			return std::nullopt;
		}

		const auto decorationLayout = xSettings->setting("Gtk/DecorationLayout");
		if (!decorationLayout.isValid()) {
			return std::nullopt;
		}

		return GtkKeywordsToTitleControlsLayout(decorationLayout.toString());
	}();

	if (xSettingsResult.has_value()) {
		return *xSettingsResult;
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	const auto portalResult = []() -> std::optional<TitleControls::Layout> {
		namespace XDP = base::Platform::XDP;

		const auto decorationLayout = XDP::ReadSetting<Glib::ustring>(
			"org.gnome.desktop.wm.preferences",
			"button-layout");

		if (!decorationLayout.has_value()) {
			return std::nullopt;
		}

		return GtkKeywordsToTitleControlsLayout(
			QString::fromStdString(*decorationLayout));
	}();

	if (portalResult.has_value()) {
		return *portalResult;
	}

	return TitleControls::Layout{
		.right = {
			TitleControls::Control::Minimize,
			TitleControls::Control::Maximize,
			TitleControls::Control::Close,
		}
	};
}

} // namespace internal
} // namespace Platform
} // namespace Ui
