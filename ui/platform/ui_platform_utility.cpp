// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/ui_platform_utility.h"

namespace Ui {
namespace Platform {
namespace internal {
namespace {

auto &CachedTitleControlsLayout() {
	using Layout = TitleControls::Layout;
	static rpl::variable<Layout> Result = TitleControlsLayout();
	return Result;
};

} // namespace

void NotifyTitleControlsLayoutChanged() {
	CachedTitleControlsLayout() = TitleControlsLayout();
}

} // namespace internal

TitleControls::Layout TitleControlsLayout() {
	return internal::CachedTitleControlsLayout().current();
}

rpl::producer<TitleControls::Layout> TitleControlsLayoutValue() {
	return internal::CachedTitleControlsLayout().value();
}

rpl::producer<TitleControls::Layout> TitleControlsLayoutChanged() {
	return internal::CachedTitleControlsLayout().changes();
}

} // namespace Platform
} // namespace Ui
