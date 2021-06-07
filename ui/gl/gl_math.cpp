// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_math.h"

namespace Ui::GL {

QVector4D Uniform(const QRect &rect, float factor) {
	return QVector4D(
		rect.x() * factor,
		rect.y() * factor,
		rect.width() * factor,
		rect.height() * factor);
}

QVector4D Uniform(const Rect &rect) {
	return QVector4D(rect.x(), rect.y(), rect.width(), rect.height());
}

QSizeF Uniform(QSize size) {
	return size;
}

Rect TransformRect(const Rect &raster, QSize viewport, float factor) {
	return {
		raster.left() * factor,
		float(viewport.height() - raster.bottom()) * factor,
		raster.width() * factor,
		raster.height() * factor,
	};
}

} // namespace Ui::GL
