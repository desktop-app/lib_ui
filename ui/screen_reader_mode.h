// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

namespace Ui {

void SetScreenReaderModeDisabled(bool disabled);
[[nodiscard]] bool ScreenReaderModeDisabled();
[[nodiscard]] bool ScreenReaderModeActive();
[[nodiscard]] rpl::producer<bool> ScreenReaderModeActiveValue();

} // namespace Ui
