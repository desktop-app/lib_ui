// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Ui {

void MarkInactivePress(not_null<QWidget*> widget, bool was);
[[nodiscard]] bool WasInactivePress(not_null<QWidget*> widget);

} // namespace Ui
