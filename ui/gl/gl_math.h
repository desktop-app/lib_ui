// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QVector4D>
#include <QtCore/QSizeF>

namespace Ui::GL {

class Rect final {
public:
	Rect(QRect rect)
	: _x(rect.x())
	, _y(rect.y())
	, _width(rect.width())
	, _height(rect.height()) {
	}

	Rect(QRectF rect)
	: _x(rect.x())
	, _y(rect.y())
	, _width(rect.width())
	, _height(rect.height()) {
	}

	Rect(float x, float y, float width, float height)
	: _x(x)
	, _y(y)
	, _width(width)
	, _height(height) {
	}

	[[nodiscard]] float x() const {
		return _x;
	}
	[[nodiscard]] float y() const {
		return _y;
	}
	[[nodiscard]] float width() const {
		return _width;
	}
	[[nodiscard]] float height() const {
		return _height;
	}
	[[nodiscard]] float left() const {
		return _x;
	}
	[[nodiscard]] float top() const {
		return _y;
	}
	[[nodiscard]] float right() const {
		return _x + _width;
	}
	[[nodiscard]] float bottom() const {
		return _y + _height;
	}

	[[nodiscard]] bool empty() const {
		return (_width <= 0) || (_height <= 0);
	}

private:
	float _x = 0;
	float _y = 0;
	float _width = 0;
	float _height = 0;

};

[[nodiscard]] QVector4D Uniform(const QRect &rect, float factor);
[[nodiscard]] QVector4D Uniform(const Rect &rect);
[[nodiscard]] QSizeF Uniform(QSize size);

[[nodiscard]] Rect TransformRect(
	const Rect &raster,
	QSize viewport,
	float factor);

} // namespace Ui::GL
