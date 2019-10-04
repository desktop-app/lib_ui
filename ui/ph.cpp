// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/ph.h"

namespace ph {
namespace {

int &PhraseCounter() {
	static auto result = 0;
	return result;
}

} // namespace

phrase::phrase(const QString &initial) : value(initial) {
	if (auto &counter = PhraseCounter()) {
		++counter;
	}
}

now_t start_phrase_count() {
	PhraseCounter() = 1;
	return now;
}

now_t check_phrase_count(int count) {
	Expects(PhraseCounter() == count + 1);

	PhraseCounter() = 0;
	return now;
}

} // namespace ph
