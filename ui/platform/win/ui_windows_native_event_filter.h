// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <Windows.h>

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Platform {

class NativeEventFilter {
public:
	explicit NativeEventFilter(not_null<RpWidget*> that);
	virtual ~NativeEventFilter();

	virtual bool filterNativeEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) = 0;

private:
	class FilterSingleton;
	friend class FilterSingleton;

	[[nodiscard]] static not_null<FilterSingleton*> Singleton();
	HWND _hwnd = nullptr;

};

} // namespace Ui::Platform
