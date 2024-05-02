// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QColor>
#include <QtGui/QPen>
#include <QtGui/QBrush>

#include <rpl/lifetime.h>

namespace style {

class palette_data;
class palette;

namespace internal {

class Color;
class OwnedColor;

class ColorData {
public:
	QColor c;
	QPen p;
	QBrush b;

	[[nodiscard]] QColor transparent() const {
		return QColor(c.red(), c.green(), c.blue(), 0);
	}

private:
	ColorData(uchar r, uchar g, uchar b, uchar a);
	ColorData(const ColorData &other) = default;
	ColorData &operator=(const ColorData &other) = default;

	void set(uchar r, uchar g, uchar b, uchar a);

	friend class Color;
	friend class OwnedColor;
	friend class style::palette;
	friend class style::palette_data;

};

class Color {
public:
	Color(Qt::Initialization = Qt::Uninitialized) {
	}
	Color(const Color &other) = default;
	Color &operator=(const Color &other) = default;

	void set(uchar r, uchar g, uchar b, uchar a) const {
		_data->set(r, g, b, a);
	}

	[[nodiscard]] operator const QBrush &() const {
		return _data->b;
	}

	[[nodiscard]] operator const QPen &() const {
		return _data->p;
	}

	[[nodiscard]] ColorData *operator->() const {
		return _data;
	}
	[[nodiscard]] ColorData *get() const {
		return _data;
	}

	[[nodiscard]] explicit operator bool() const {
		return !!_data;
	}

	class Proxy;
	[[nodiscard]] Proxy operator[](
		const style::palette &paletteOverride) const;

private:
	friend class OwnedColor;
	friend class style::palette;
	friend class style::palette_data;

	Color(ColorData *data) : _data(data) {
	}

	ColorData *_data = nullptr;

};

class OwnedColor final {
public:
	explicit OwnedColor(const QColor &color)
	: _data(color.red(), color.green(), color.blue(), color.alpha())
	, _color(&_data) {
	}

	OwnedColor(const OwnedColor &other)
	: _data(other._data)
	, _color(&_data) {
	}

	OwnedColor &operator=(const OwnedColor &other) {
		_data = other._data;
		return *this;
	}

	void update(const QColor &color) {
		_data.set(color.red(), color.green(), color.blue(), color.alpha());
	}

	[[nodiscard]] const Color &color() const {
		return _color;
	}

private:
	ColorData _data;
	Color _color;

};

class ComplexColor final {
public:
	explicit ComplexColor(Fn<QColor()> generator)
	: _owned(generator())
	, _generator(std::move(generator)) {
		subscribeToPaletteChanges();
	}

	ComplexColor(const ComplexColor &other)
	: _owned(other._owned)
	, _generator(other._generator) {
		subscribeToPaletteChanges();
	}

	ComplexColor &operator=(const ComplexColor &other) {
		_owned = other._owned;
		_generator = other._generator;
		return *this;
	}

	[[nodiscard]] const Color &color() const {
		return _owned.color();
	}
	void refresh() {
		_owned.update(_generator());
	}

private:
	void subscribeToPaletteChanges();

	OwnedColor _owned;
	Fn<QColor()> _generator;
	rpl::lifetime _lifetime;

};

class Color::Proxy {
public:
	Proxy(Color color) : _color(color) {
	}
	Proxy(const Proxy &other) = default;

	[[nodiscard]] operator const QBrush &() const { return _color; }
	[[nodiscard]] operator const QPen &() const { return _color; }
	[[nodiscard]] ColorData *operator->() const { return _color.get(); }
	[[nodiscard]] ColorData *get() const { return _color.get(); }
	[[nodiscard]] explicit operator bool() const { return !!_color; }
	[[nodiscard]] Color clone() const { return _color; }

private:
	Color _color;

};

inline bool operator==(Color a, Color b) {
	return a->c == b->c;
}

inline bool operator!=(Color a, Color b) {
	return a->c != b->c;
}

} // namespace internal
} // namespace style
