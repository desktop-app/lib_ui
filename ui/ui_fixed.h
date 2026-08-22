// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtCore/QtGlobal>

#include <compare>

namespace Ui {

// A 26.6 fixed point value, bit-compatible with Qt's private QFixed.
//
// It exists so that public headers - ui/style/style_core_font.h and
// ui/text/text.h among them - can carry fixed point metrics without dragging
// <private/qfixed_p.h> into every translation unit that includes them. The
// text engine keeps using QFixed internally and converts on the boundary with
// raw(), so the representation must stay identical: every operation below is
// copied from qfixed_p.h, including the rounding in toInt().
class Fixed final {
public:
	constexpr Fixed() = default;
	constexpr Fixed(int value) : _value(value * 64) {
	}

	[[nodiscard]] constexpr static Fixed FromRaw(int raw) {
		auto result = Fixed();
		result._value = raw;
		return result;
	}
	[[nodiscard]] constexpr static Fixed FromReal(qreal value) {
		return FromRaw(int(value * qreal(64)));
	}

	[[nodiscard]] constexpr int raw() const {
		return _value;
	}
	[[nodiscard]] constexpr int toInt() const {
		return (((_value) + 32) & -64) >> 6;
	}
	[[nodiscard]] constexpr qreal toReal() const {
		return qreal(_value) / qreal(64);
	}
	// Return Fixed, like QFixed does - callers chain .ceil().toInt().
	[[nodiscard]] constexpr Fixed floor() const {
		return FromRaw(_value & -64);
	}
	[[nodiscard]] constexpr Fixed ceil() const {
		return FromRaw((_value + 63) & -64);
	}

	[[nodiscard]] friend constexpr Fixed abs(Fixed value) {
		return FromRaw((value._value < 0) ? -value._value : value._value);
	}

	[[nodiscard]] friend constexpr Fixed operator+(Fixed a, Fixed b) {
		return FromRaw(a._value + b._value);
	}
	[[nodiscard]] friend constexpr Fixed operator-(Fixed a, Fixed b) {
		return FromRaw(a._value - b._value);
	}
	[[nodiscard]] constexpr Fixed operator-() const {
		return FromRaw(-_value);
	}
	[[nodiscard]] constexpr Fixed operator*(int value) const {
		return FromRaw(_value * value);
	}
	[[nodiscard]] friend constexpr Fixed operator*(int value, Fixed fixed) {
		return FromRaw(fixed._value * value);
	}
	[[nodiscard]] constexpr Fixed operator*(Fixed other) const {
		return FromRaw(int(
			(qlonglong(_value) * qlonglong(other._value)) >> 6));
	}
	[[nodiscard]] constexpr Fixed operator/(int value) const {
		return FromRaw(_value / value);
	}
	[[nodiscard]] constexpr Fixed operator/(Fixed other) const {
		return FromRaw(int((qlonglong(_value) << 6) / other._value));
	}
	constexpr Fixed &operator+=(Fixed other) {
		_value += other._value;
		return *this;
	}
	constexpr Fixed &operator-=(Fixed other) {
		_value -= other._value;
		return *this;
	}

	friend constexpr auto operator<=>(Fixed, Fixed) = default;
	friend constexpr bool operator==(Fixed, Fixed) = default;

private:
	int _value = 0;

};

} // namespace Ui
