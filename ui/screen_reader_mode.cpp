// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/screen_reader_mode.h"

#include "base/screen_reader_state.h"

namespace Ui {
namespace {

rpl::variable<bool> &DisabledVariable() {
	static auto result = rpl::variable<bool>(false);
	return result;
}

} // namespace

void SetScreenReaderModeDisabled(bool disabled) {
	DisabledVariable() = disabled;
}

bool ScreenReaderModeDisabled() {
	return DisabledVariable().current();
}

bool ScreenReaderModeActive() {
	return !DisabledVariable().current()
		&& base::ScreenReaderState::Instance()->active();
}

rpl::producer<bool> ScreenReaderModeActiveValue() {
	return rpl::combine(
		DisabledVariable().value(),
		base::ScreenReaderState::Instance()->activeValue()
	) | rpl::map([](bool disabled, bool detected) {
		return !disabled && detected;
	}) | rpl::distinct_until_changed();
}

} // namespace Ui
