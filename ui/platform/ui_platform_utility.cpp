// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/ui_platform_utility.h"

namespace Ui {
namespace Platform {
namespace {

rpl::event_stream<> TitleControlsLayoutChanges;

} // namespace

rpl::producer<> TitleControlsLayoutChanged() {
	return TitleControlsLayoutChanges.events();
}

void NotifyTitleControlsLayoutChanged() {
	TitleControlsLayoutChanges.fire({});
}

} // namespace Platform
} // namespace Ui
