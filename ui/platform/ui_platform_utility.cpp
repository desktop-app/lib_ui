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

rpl::event_stream<TitleControls::Layout> TitleControlsLayoutChanges;

} // namespace

rpl::producer<TitleControls::Layout> TitleControlsLayoutValue() {
	return rpl::single(
		TitleControlsLayout()
	) | rpl::then(
		TitleControlsLayoutChanged()
	);
}

rpl::producer<TitleControls::Layout> TitleControlsLayoutChanged() {
	return TitleControlsLayoutChanges.events();
}

void NotifyTitleControlsLayoutChanged() {
	TitleControlsLayoutChanges.fire_copy(TitleControlsLayout());
}

} // namespace Platform
} // namespace Ui
