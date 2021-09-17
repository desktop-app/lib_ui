// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_palette_colorizer.h"

namespace style {
namespace {

constexpr auto kEnoughLightnessForContrast = 64;

void FillColorizeResult(uchar &r, uchar &g, uchar &b, const QColor &color) {
	auto nowR = 0;
	auto nowG = 0;
	auto nowB = 0;
	color.getRgb(&nowR, &nowG, &nowB);
	r = uchar(nowR);
	g = uchar(nowG);
	b = uchar(nowB);
}

[[nodiscard]] std::optional<colorizer::Color> colorize(
		const colorizer::Color &color,
		const colorizer &with) {
	const auto changeColor = std::abs(color.hue - with.was.hue)
		< with.hueThreshold;
	if (!changeColor) {
		return std::nullopt;
	}
	const auto nowHue = color.hue + (with.now.hue - with.was.hue);
	const auto nowSaturation = ((color.saturation > with.was.saturation)
		&& (with.now.saturation > with.was.saturation))
		? (((with.now.saturation * (255 - with.was.saturation))
			+ ((color.saturation - with.was.saturation)
				* (255 - with.now.saturation)))
			/ (255 - with.was.saturation))
		: ((color.saturation != with.was.saturation)
			&& (with.was.saturation != 0))
		? ((color.saturation * with.now.saturation)
			/ with.was.saturation)
		: with.now.saturation;
	const auto nowValue = (color.value > with.was.value)
		? (((with.now.value * (255 - with.was.value))
			+ ((color.value - with.was.value)
				* (255 - with.now.value)))
			/ (255 - with.was.value))
		: (color.value < with.was.value)
		? ((color.value * with.now.value)
			/ with.was.value)
		: with.now.value;
	return colorizer::Color{
		((nowHue + 360) % 360),
		nowSaturation,
		nowValue
	};
}

} // namespace

QColor ColorFromHex(std::string_view hex) {
	Expects(hex.size() == 6);

	const auto component = [](char a, char b) {
		const auto convert = [](char ch) {
			Expects((ch >= '0' && ch <= '9')
				|| (ch >= 'A' && ch <= 'F')
				|| (ch >= 'a' && ch <= 'f'));

			return (ch >= '0' && ch <= '9')
				? int(ch - '0')
				: int(ch - ((ch >= 'A' && ch <= 'F') ? 'A' : 'a') + 10);
		};
		return convert(a) * 16 + convert(b);
	};

	return QColor(
		component(hex[0], hex[1]),
		component(hex[2], hex[3]),
		component(hex[4], hex[5]));
};

void colorize(uchar &r, uchar &g, uchar &b, const colorizer &with) {
	const auto changed = colorize(QColor(int(r), int(g), int(b)), with);
	if (changed) {
		FillColorizeResult(r, g, b, *changed);
	}
}

void colorize(
		QLatin1String name,
		uchar &r,
		uchar &g,
		uchar &b,
		const colorizer &with) {
	if (with.ignoreKeys.contains(name)) {
		return;
	}

	const auto i = with.keepContrast.find(name);
	if (i == end(with.keepContrast)) {
		colorize(r, g, b, with);
	} else {
		colorize(i->second, r, g, b, with);
	}
}

void colorize(
		const std::pair<colorizer::Color, colorizer::Color> &contrast,
		uchar &r,
		uchar &g,
		uchar &b,
		const colorizer &with) {
	const auto check = contrast.first;
	const auto rgb = QColor(int(r), int(g), int(b));
	const auto changed = colorize(rgb, with);
	const auto checked = colorize(check, with).value_or(check);
	const auto lightness = [](QColor hsv) {
		return hsv.value() - (hsv.value() * hsv.saturation()) / 511;
	};
	const auto changedLightness = lightness(changed.value_or(rgb).toHsv());
	const auto checkedLightness = lightness(
		QColor::fromHsv(checked.hue, checked.saturation, checked.value));
	const auto delta = std::abs(changedLightness - checkedLightness);
	if (delta >= kEnoughLightnessForContrast) {
		if (changed) {
			FillColorizeResult(r, g, b, *changed);
		}
		return;
	}
	const auto replace = contrast.second;
	const auto result = colorize(replace, with).value_or(replace);
	FillColorizeResult(
		r,
		g,
		b,
		QColor::fromHsv(result.hue, result.saturation, result.value));
}

void colorize(uint32 &pixel, const colorizer &with) {
	const auto chars = reinterpret_cast<uchar*>(&pixel);
	colorize(chars[2], chars[1], chars[0], with);
}

void colorize(QImage &image, const colorizer &with) {
	image = std::move(image).convertToFormat(QImage::Format_ARGB32);
	const auto bytes = image.bits();
	const auto bytesPerLine = image.bytesPerLine();
	for (auto line = 0; line != image.height(); ++line) {
		const auto ints = reinterpret_cast<uint32*>(
			bytes + line * bytesPerLine);
		const auto end = ints + image.width();
		for (auto p = ints; p != end; ++p) {
			colorize(*p, with);
		}
	}
}

std::optional<QColor> colorize(const QColor &color, const colorizer &with) {
	auto hue = 0;
	auto saturation = 0;
	auto lightness = 0;
	auto alpha = 0;
	color.getHsv(&hue, &saturation, &lightness, &alpha);
	const auto result = colorize(
		colorizer::Color{ hue, saturation, lightness },
		with);
	if (!result) {
		return std::nullopt;
	}
	const auto &hsv = *result;
	return QColor::fromHsv(hsv.hue, hsv.saturation, hsv.value, alpha);
}

QByteArray colorize(
		QLatin1String hexColor,
		const style::colorizer &with) {
	Expects(hexColor.size() == 7 || hexColor.size() == 9);

	auto color = ColorFromHex(std::string_view(hexColor.data() + 1, 6));
	const auto changed = colorize(color, with).value_or(color).toRgb();

	auto result = QByteArray();
	result.reserve(hexColor.size());
	result.append(hexColor.data()[0]);
	const auto addHex = [&](int code) {
		if (code >= 0 && code < 10) {
			result.append('0' + code);
		} else if (code >= 10 && code < 16) {
			result.append('a' + (code - 10));
		}
	};
	const auto addValue = [&](int code) {
		addHex(code / 16);
		addHex(code % 16);
	};
	addValue(changed.red());
	addValue(changed.green());
	addValue(changed.blue());
	if (hexColor.size() == 9) {
		result.append(hexColor.data()[7]);
		result.append(hexColor.data()[8]);
	}
	return result;
}

} // namespace style
