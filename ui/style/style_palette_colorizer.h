// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace style {

struct colorizer {
	struct Color {
		int hue = 0;
		int saturation = 0;
		int value = 0;
	};
	int hueThreshold = 0;
	int lightnessMin = 0;
	int lightnessMax = 255;
	Color was;
	Color now;
	base::flat_set<QLatin1String> ignoreKeys;
	base::flat_map<QLatin1String, std::pair<Color, Color>> keepContrast;

	explicit operator bool() const {
		return (hueThreshold > 0);
	}
};

[[nodiscard]] QColor ColorFromHex(std::string_view hex);

void colorize(
	uchar &r,
	uchar &g,
	uchar &b,
	const colorizer &with);
void colorize(
	QLatin1String name,
	uchar &r,
	uchar &g,
	uchar &b,
	const colorizer &with);
void colorize(
	const std::pair<colorizer::Color, colorizer::Color> &contrast,
	uchar &r,
	uchar &g,
	uchar &b,
	const colorizer &with);
void colorize(QImage &image, const colorizer &with);

[[nodiscard]] std::optional<QColor> colorize(
	const QColor &color,
	const colorizer &with);

[[nodiscard]] QByteArray colorize(
	QLatin1String hexColor,
	const colorizer &with);

} // namespace style
