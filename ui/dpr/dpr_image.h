// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <rpl/details/callable.h>

#include <QtGui/QImage>
#include <QtGui/QPainter>

namespace dpr {

// Validate(_cache, devicePixelRatioF(), size, [&](QPainter &p, QSize size) {
//	... paint using p ...
// }, (_cacheKey != cacheKey()), Qt::transparent);

template <typename Generator>
void Validate(
		QImage &image,
		double ratio,
		QSize size,
		Generator &&generator,
		bool force,
		std::optional<QColor> fill = {},
		bool setResultRatio = true) {
	size *= ratio;
	const auto sizeChanged = (image.size() != size);
	if (sizeChanged || force) {
		if (sizeChanged) {
			image = QImage(size, QImage::Format_ARGB32_Premultiplied);
		}
		if (fill) {
			image.fill(*fill);
		}
		image.setDevicePixelRatio(1.);
		auto p = QPainter(&image);
		using namespace rpl::details;
		if constexpr (is_callable_plain_v<Generator, QPainter&, QSize>) {
			generator(p, size);
		} else {
			generator(p);
		}
	}
	image.setDevicePixelRatio(setResultRatio ? ratio : 1.);
}

} // namespace dpr
