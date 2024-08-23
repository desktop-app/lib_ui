// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

#include <private/qfixed_p.h>

namespace Ui::Text {

class Word final {
public:
	Word() = default;
	Word(
		uint16 position,
		bool continuation,
		bool newline,
		QFixed width,
		QFixed rbearing,
		QFixed rpadding = 0)
	: _position(position)
	, _rbearing_modulus(std::min(std::abs(rbearing.value()), 0x7FFF))
	, _rbearing_positive(rbearing.value() > 0 ? 1 : 0)
	, _continuation(continuation ? 1 : 0)
	, _newline(newline ? 1 : 0)
	, _width(width)
	, _rpadding(rpadding) {
	}

	[[nodiscard]] bool newline() const {
		return _newline != 0;
	}
	[[nodiscard]] bool continuation() const {
		return _continuation != 0;
	}

	[[nodiscard]] uint16 position() const {
		return _position;
	}
	[[nodiscard]] QFixed f_rbearing() const {
		return QFixed::fromFixed(
			int(_rbearing_modulus) * (_rbearing_positive ? 1 : -1));
	}
	[[nodiscard]] QFixed f_width() const {
		return _width;
	}
	[[nodiscard]] QFixed f_rpadding() const {
		return _rpadding;
	}

	void add_rpadding(QFixed padding) {
		_rpadding += padding;
	}

private:
	uint16 _position = 0;
	uint16 _rbearing_modulus : 13 = 0;
	uint16 _rbearing_positive : 1 = 0;
	uint16 _continuation : 1 = 0;
	uint16 _newline : 1 = 0;
	QFixed _width;

	// Right padding: spaces after the last content of the block (like a word).
	// This holds spaces after the end of the block, for example a text ending
	// with a space before a link has started. If text block has a leading spaces
	// (for example a text block after a link block) it is prepended with an empty
	// word that holds those spaces as a right padding.
	QFixed _rpadding;

};

using Words = std::vector<Word>;

[[nodiscard]] inline uint16 CountPosition(Words::const_iterator i) {
	return i->position();
}

} // namespace Ui::Text
