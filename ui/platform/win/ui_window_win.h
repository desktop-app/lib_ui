// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_window.h"
#include "ui/platform/win/ui_window_shadow_win.h"

#include <windef.h>

namespace Ui {
namespace Platform {

class TitleWidget;

class WindowHelper final : public BasicWindowHelper {
public:
	explicit WindowHelper(not_null<RpWidget*> window);
	~WindowHelper();

	not_null<RpWidget*> body() override;
	void setTitle(const QString &title) override;
	void setTitleStyle(const style::WindowTitle &st) override;
	void setMinimumSize(QSize size) override;
	void setFixedSize(QSize size) override;
	void setGeometry(QRect rect) override;
	void showFullScreen() override;
	void showNormal() override;

private:
	class NativeFilter;
	friend class NativeFilter;

	void init();
	void updateMargins();
	void updateSystemMenu();
	void updateSystemMenu(Qt::WindowState state);
	[[nodiscard]] bool handleNativeEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);
	[[nodiscard]] bool fixedSize() const;

	static not_null<NativeFilter*> GetNativeFilter();

	const HWND _handle = nullptr;
	const not_null<TitleWidget*> _title;
	const not_null<RpWidget*> _body;
	WindowShadow _shadow;
	bool _updatingMargins = false;
	QMargins _marginsDelta;
	HMENU _menu = nullptr;
	bool _isFullScreen = false;

};

[[nodiscard]] HWND GetWindowHandle(not_null<RpWidget*> widget);

} // namespace Platform
} // namespace Ui
