// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/dpr/dpr_icon.h"

namespace dpr {

QImage IconFrame(
		const style::icon &icon,
		const QColor &color,
		double ratio) {
	const auto scale = style::Scale() * ratio;
	const auto use = (scale > 200. || style::DevicePixelRatio() > 2)
		? (300 / style::DevicePixelRatio())
		: (scale > 100.)
		? (200 / style::DevicePixelRatio())
		: (100 / style::DevicePixelRatio());
	auto image = icon.instance(color, use);
	image.setDevicePixelRatio(1.);
	const auto desired = icon.size() * ratio;
	return (image.size() == desired)
		? image
		: image.scaled(
			desired,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
}

} // namespace dpr
