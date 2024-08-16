// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/platform/win/base_windows_rpcndr_h.h"
#include "base/platform/win/base_windows_gdiplus_h.h"
#include "base/flags.h"

class QColor;

namespace Ui {

class RpWidget;

namespace Platform {

class WindowShadow final {
public:
	WindowShadow(not_null<RpWidget*> window, QColor color);
	~WindowShadow();

	enum class Change {
		Moved = (1 << 0),
		Resized = (1 << 1),
		Activate = (1 << 2),
		Deactivate = (1 << 3),
		Hidden = (1 << 4),
		Shown = (1 << 5),
	};
	friend inline constexpr bool is_flag_type(Change) { return true; };
	using Changes = base::flags<Change>;

	void setColor(QColor color);
	void update(Changes changes, WINDOWPOS *pos = nullptr);
	void updateWindow(int i, POINT *p, SIZE *s = nullptr);
	void setResizeEnabled(bool enabled);

private:
	enum class Direction {
		Horizontal = (1 << 0),
		Vertical = (1 << 1),
		All = Horizontal | Vertical,
	};
	friend inline constexpr bool is_flag_type(Direction) { return true; };
	using Directions = base::flags<Direction>;

	static LRESULT CALLBACK WindowCallback(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam);

	LRESULT windowCallback(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam);
	[[nodiscard]] bool working() const;
	void destroy();
	void create();
	void initBlend();
	void updateColor();
	void initCorners(Directions directions = Direction::All);
	void verCorners(int h, Gdiplus::Graphics *pgraphics1, Gdiplus::Graphics *pgraphics3);
	void horCorners(int w, Gdiplus::Graphics *pgraphics0, Gdiplus::Graphics *pgraphics2);
	[[nodiscard]] Gdiplus::Color getColor(uchar alpha) const;
	[[nodiscard]] Gdiplus::SolidBrush getBrush(uchar alpha) const;
	[[nodiscard]] Gdiplus::Pen getPen(uchar alpha) const;

	const not_null<RpWidget*> _window;
	HWND _handle = nullptr;

	int _x = 0;
	int _y = 0;
	int _w = 0;
	int _h = 0;
	int _metaSize = 0;
	int _fullsize = 0;
	int _size = 0;
	int _shift = 0;
	std::vector<BYTE> _alphas;
	std::vector<BYTE> _colors;

	bool _hidden = true;
	bool _resizeEnabled = true;

	HWND _handles[4] = { nullptr };
	HDC _contexts[4] = { nullptr };
	HBITMAP _bitmaps[4] = { nullptr };
	HDC _screenContext = nullptr;
	int _widthMax = 0;
	int _heightMax = 0;
	BLENDFUNCTION _blend;

	BYTE _r = 0;
	BYTE _g = 0;
	BYTE _b = 0;
	COLORREF _noKeyColor = RGB(255, 255, 255);

};

} // namespace Platform
} // namespace Ui
