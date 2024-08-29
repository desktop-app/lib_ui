// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_window_shadow_win.h"

#include "ui/rp_widget.h"
#include "ui/platform/win/ui_window_win.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

#include <windowsx.h>

// WM_POINTER support from Windows 8 onwards (WINVER >= 0x0602)
#ifndef WM_POINTERUPDATE
#  define WM_NCPOINTERUPDATE 0x0241
#  define WM_NCPOINTERDOWN   0x0242
#  define WM_NCPOINTERUP     0x0243
#  define WM_POINTERUPDATE   0x0245
#  define WM_POINTERDOWN     0x0246
#  define WM_POINTERUP       0x0247
#  define WM_POINTERENTER    0x0249
#  define WM_POINTERLEAVE    0x024A
#  define WM_POINTERACTIVATE 0x024B
#  define WM_POINTERCAPTURECHANGED 0x024C
#  define WM_POINTERWHEEL    0x024E
#  define WM_POINTERHWHEEL   0x024F
#endif // WM_POINTERUPDATE

namespace Ui {
namespace Platform {
namespace {

base::flat_map<HWND, not_null<WindowShadow*>> ShadowByHandle;

} // namespace

WindowShadow::WindowShadow(not_null<RpWidget*> window, QColor color)
: _window(window)
, _handle(GetWindowHandle(window)) {
	init(color);
}

WindowShadow::~WindowShadow() {
	destroy();
}

void WindowShadow::setColor(QColor value) {
	_r = value.red();
	_g = value.green();
	_b = value.blue();
	if (!working()) {
		return;
	}

	auto brush = getBrush(_alphas[0]);
	for (auto i = 0; i != 4; ++i) {
		auto graphics = Gdiplus::Graphics(_contexts[i]);
		graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		if ((i % 2) && _h || !(i % 2) && _w) {
			const auto width = (i % 2) ? _size : _w;
			const auto height = (i % 2) ? _h : _size;
			graphics.FillRectangle(&brush, 0, 0, width, height);
		}
	}
	initCorners();

	_x = _y = _w = _h = 0;
	update(Change::Moved | Change::Resized);
}

bool WindowShadow::working() const {
	return (_handle != nullptr) && (_handles[0] != nullptr);
}

void WindowShadow::destroy() {
	for (int i = 0; i < 4; ++i) {
		if (_contexts[i]) {
			DeleteDC(_contexts[i]);
			_contexts[i] = nullptr;
		}
		if (_bitmaps[i]) {
			DeleteObject(_bitmaps[i]);
			_bitmaps[i] = nullptr;
		}
		if (_handles[i]) {
			ShadowByHandle.remove(_handles[i]);
			DestroyWindow(_handles[i]);
			_handles[i] = nullptr;
		}
	}
	if (_screenContext) {
		ReleaseDC(nullptr, _screenContext);
		_screenContext = nullptr;
	}
}

void WindowShadow::init(QColor color) {
	if (!_handle) {
		return;
	}

	initBlend();

	_fullsize = st::windowShadow.width();
	_shift = st::windowShadowShift;
	auto cornersImage = QImage(
		QSize(_fullsize, _fullsize),
		QImage::Format_ARGB32_Premultiplied);
	cornersImage.fill(QColor(0, 0, 0));
	{
		QPainter p(&cornersImage);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		st::windowShadow.paint(p, 0, 0, _fullsize, QColor(255, 255, 255));
	}
	if (style::RightToLeft()) {
		cornersImage = cornersImage.mirrored(true, false);
	}
	const auto pixels = cornersImage.bits();
	const auto pixel = [&](int x, int y) {
		if (x < 0 || y < 0) {
			return 0;
		}
		const auto data = pixels
			+ (cornersImage.bytesPerLine() * y)
			+ (sizeof(uint32) * x);
		return int(data[0]);
	};

	_metaSize = _fullsize + 2 * _shift;
	_alphas.reserve(_metaSize);
	_colors.reserve(_metaSize * _metaSize);
	for (auto j = 0; j != _metaSize; ++j) {
		for (auto i = 0; i != _metaSize; ++i) {
			const auto value = pixel(i - 2 * _shift, j - 2 * _shift);
			_colors.push_back(uchar(std::max(value, 1)));
		}
	}
	auto previous = uchar(0);
	for (auto i = 0; i != _metaSize; ++i) {
		const auto alpha = _colors[(_metaSize - 1) * _metaSize + i];
		if (alpha < previous) {
			break;
		}
		_alphas.push_back(alpha);
		previous = alpha;
	}
	_size = _alphas.size() - 2 * _shift;

	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::Status gdiRes = Gdiplus::GdiplusStartup(
		&gdiplusToken,
		&gdiplusStartupInput,
		NULL);

	if (gdiRes != Gdiplus::Ok) {
		return;
	}

	_screenContext = GetDC(nullptr);
	if (!_screenContext) {
		return;
	}

	const auto avail = QApplication::primaryScreen()->availableGeometry();
	_widthMax = std::max(avail.width(), 1);
	_heightMax = std::max(avail.height(), 1);

	static const auto instance = (HINSTANCE)GetModuleHandle(nullptr);
	static const auto className = u"WindowShadow"_q;
	static const auto wcharClassName = className.toStdWString();
	static const auto registered = [] {
		auto wc = WNDCLASSEX();
		wc.cbSize = sizeof(wc);
		wc.style = 0;
		wc.lpfnWndProc = WindowCallback;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = instance;
		wc.hIcon = 0;
		wc.hCursor = 0;
		wc.hbrBackground = 0;
		wc.lpszMenuName = NULL;
		wc.lpszClassName = wcharClassName.c_str();
		wc.hIconSm = 0;
		return RegisterClassEx(&wc) ? true : false;
	}();
	if (!registered) {
		return;
	}
	for (auto i = 0; i != 4; ++i) {
		_handles[i] = CreateWindowEx(
			WS_EX_LAYERED | WS_EX_TOOLWINDOW,
			wcharClassName.c_str(),
			0,
			WS_POPUP,
			0,
			0,
			0,
			0,
			0,
			0,
			instance,
			0);
		if (!_handles[i]) {
			destroy();
			return;
		}
		ShadowByHandle.emplace(_handles[i], this);
		SetWindowLongPtr(_handles[i], GWLP_HWNDPARENT, (LONG_PTR)_handle);

		_contexts[i] = CreateCompatibleDC(_screenContext);
		if (!_contexts[i]) {
			destroy();
			return;
		}

		const auto width = (i % 2) ? _size : _widthMax;
		const auto height = (i % 2) ? _heightMax : _size;
		_bitmaps[i] = CreateCompatibleBitmap(_screenContext, width, height);
		if (!_bitmaps[i]) {
			return;
		}

		SelectObject(_contexts[i], _bitmaps[i]);
	}
	setColor(color);
}

void WindowShadow::initCorners(Directions directions) {
	const auto hor = (directions & Direction::Horizontal);
	const auto ver = (directions & Direction::Vertical);
	auto graphics0 = Gdiplus::Graphics(_contexts[0]);
	auto graphics1 = Gdiplus::Graphics(_contexts[1]);
	auto graphics2 = Gdiplus::Graphics(_contexts[2]);
	auto graphics3 = Gdiplus::Graphics(_contexts[3]);
	graphics0.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	graphics1.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	graphics2.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	graphics3.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

	auto brush = getBrush(_alphas[0]);
	if (hor) {
		graphics0.FillRectangle(&brush, 0, 0, _fullsize - (_size - _shift), 2 * _shift);
	}

	if (ver) {
		graphics1.FillRectangle(&brush, 0, 0, _size, 2 * _shift);
		graphics3.FillRectangle(&brush, 0, 0, _size, 2 * _shift);
		graphics1.FillRectangle(&brush, _size - _shift, 2 * _shift, _shift, _fullsize);
		graphics3.FillRectangle(&brush, 0, 2 * _shift, _shift, _fullsize);
	}

	if (hor) {
		for (int j = 2 * _shift; j < _size; ++j) {
			for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
				brush.SetColor(getColor(_colors[j * _metaSize + k + (_size + _shift)]));
				graphics0.FillRectangle(&brush, k, j, 1, 1);
				graphics2.FillRectangle(&brush, k, _size - (j - 2 * _shift) - 1, 1, 1);
			}
		}
		for (int j = _size; j < _size + 2 * _shift; ++j) {
			for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
				brush.SetColor(getColor(_colors[j * _metaSize + k + (_size + _shift)]));
				graphics2.FillRectangle(&brush, k, _size - (j - 2 * _shift) - 1, 1, 1);
			}
		}
	}
	if (ver) {
		for (int j = 2 * _shift; j < _fullsize + 2 * _shift; ++j) {
			for (int k = _shift; k < _size; ++k) {
				brush.SetColor(getColor(_colors[j * _metaSize + (k + _shift)]));
				graphics1.FillRectangle(&brush, _size - k - 1, j, 1, 1);
				graphics3.FillRectangle(&brush, k, j, 1, 1);
			}
		}
	}
}

void WindowShadow::verCorners(int h, Gdiplus::Graphics *pgraphics1, Gdiplus::Graphics *pgraphics3) {
	auto brush = getBrush(_alphas[0]);
	pgraphics1->FillRectangle(&brush, _size - _shift, h - _fullsize, _shift, _fullsize);
	pgraphics3->FillRectangle(&brush, 0, h - _fullsize, _shift, _fullsize);
	for (int j = 0; j < _fullsize; ++j) {
		for (int k = _shift; k < _size; ++k) {
			brush.SetColor(getColor(_colors[(j + 2 * _shift) * _metaSize + k + _shift]));
			pgraphics1->FillRectangle(&brush, _size - k - 1, h - j - 1, 1, 1);
			pgraphics3->FillRectangle(&brush, k, h - j - 1, 1, 1);
		}
	}
}

void WindowShadow::horCorners(int w, Gdiplus::Graphics *pgraphics0, Gdiplus::Graphics *pgraphics2) {
	auto brush = getBrush(_alphas[0]);
	pgraphics0->FillRectangle(&brush, w - 2 * _size - (_fullsize - (_size - _shift)), 0, _fullsize - (_size - _shift), 2 * _shift);
	for (int j = 2 * _shift; j < _size; ++j) {
		for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
			brush.SetColor(getColor(_colors[j * _metaSize + k + (_size + _shift)]));
			pgraphics0->FillRectangle(&brush, w - 2 * _size - k - 1, j, 1, 1);
			pgraphics2->FillRectangle(&brush, w - 2 * _size - k - 1, _size - (j - 2 * _shift) - 1, 1, 1);
		}
	}
	for (int j = _size; j < _size + 2 * _shift; ++j) {
		for (int k = 0; k < _fullsize - (_size - _shift); ++k) {
			brush.SetColor(getColor(_colors[j * _metaSize + k + (_size + _shift)]));
			pgraphics2->FillRectangle(&brush, w - 2 * _size - k - 1, _size - (j - 2 * _shift) - 1, 1, 1);
		}
	}
}

Gdiplus::Color WindowShadow::getColor(uchar alpha) const {
	return Gdiplus::Color(BYTE(alpha), _r, _g, _b);
}

Gdiplus::SolidBrush WindowShadow::getBrush(uchar alpha) const {
	return Gdiplus::SolidBrush(getColor(alpha));
}

Gdiplus::Pen WindowShadow::getPen(uchar alpha) const {
	return Gdiplus::Pen(getColor(alpha));
}

void WindowShadow::update(Changes changes, WINDOWPOS *pos) {
	if (!working()) {
		return;
	}

	if (changes == Changes(Change::Activate)) {
		for (int i = 0; i < 4; ++i) {
			SetWindowPos(_handles[i], _handle, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		return;
	}

	if (changes & Change::Hidden) {
		if (!_hidden) {
			for (int i = 0; i < 4; ++i) {
				_hidden = true;
				ShowWindow(_handles[i], SW_HIDE);
			}
		}
		return;
	}
	if (_window->isHidden()) {
		return;
	}

	int x = _x, y = _y, w = _w, h = _h;
	if (pos && (!(pos->flags & SWP_NOMOVE) || !(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOREPOSITION))) {
		if (!(pos->flags & SWP_NOMOVE)) {
			x = pos->x - _size;
			y = pos->y - _size;
		} else if (pos->flags & SWP_NOSIZE) {
			for (int i = 0; i < 4; ++i) {
				SetWindowPos(_handles[i], _handle, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
			return;
		}
		if (!(pos->flags & SWP_NOSIZE)) {
			w = pos->cx + 2 * _size;
			h = pos->cy + 2 * _size;
		}
	} else {
		RECT r;
		GetWindowRect(_handle, &r);
		x = r.left - _size;
		y = r.top - _size;
		w = r.right + _size - x;
		h = r.bottom + _size - y;
	}
	if (h < 2 * _fullsize + 2 * _shift) {
		h = 2 * _fullsize + 2 * _shift;
	}
	if (w < 2 * (_fullsize + _shift)) {
		w = 2 * (_fullsize + _shift);
	}

	if (w != _w) {
		int from = (_w > 2 * (_fullsize + _shift)) ? (_w - _size - _fullsize - _shift) : (_fullsize - (_size - _shift));
		int to = w - _size - _fullsize - _shift;
		if (w > _widthMax) {
			from = _fullsize - (_size - _shift);
			do {
				_widthMax *= 2;
			} while (w > _widthMax);
			for (int i = 0; i < 4; i += 2) {
				DeleteObject(_bitmaps[i]);
				_bitmaps[i] = CreateCompatibleBitmap(_screenContext, _widthMax, _size);
				SelectObject(_contexts[i], _bitmaps[i]);
			}
			initCorners(Direction::Horizontal);
		}
		Gdiplus::Graphics graphics0(_contexts[0]);
		Gdiplus::Graphics graphics2(_contexts[2]);
		graphics0.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		graphics2.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		auto brush = getBrush(_alphas[0]);
		if (to > from) {
			graphics0.FillRectangle(&brush, from, 0, to - from, 2 * _shift);
			for (int i = 2 * _shift; i < _size; ++i) {
				auto pen = getPen(_alphas[i]);
				graphics0.DrawLine(&pen, from, i, to, i);
				graphics2.DrawLine(&pen, from, _size - (i - 2 * _shift) - 1, to, _size - (i - 2 * _shift) - 1);
			}
			for (int i = _size; i < _size + 2 * _shift; ++i) {
				auto pen = getPen(_alphas[i]);
				graphics2.DrawLine(&pen, from, _size - (i - 2 * _shift) - 1, to, _size - (i - 2 * _shift) - 1);
			}
		}
		if (_w > w) {
			graphics0.FillRectangle(&brush, w - _size - _fullsize - _shift, 0, _fullsize - (_size - _shift), _size);
			graphics2.FillRectangle(&brush, w - _size - _fullsize - _shift, 0, _fullsize - (_size - _shift), _size);
		}
		horCorners(w, &graphics0, &graphics2);
		POINT p0 = { x + _size, y }, p2 = { x + _size, y + h - _size }, f = { 0, 0 };
		SIZE s = { w - 2 * _size, _size };
		updateWindow(0, &p0, &s);
		updateWindow(2, &p2, &s);
	} else if (x != _x || y != _y) {
		POINT p0 = { x + _size, y }, p2 = { x + _size, y + h - _size };
		updateWindow(0, &p0);
		updateWindow(2, &p2);
	} else if (h != _h) {
		POINT p2 = { x + _size, y + h - _size };
		updateWindow(2, &p2);
	}

	if (h != _h) {
		int from = (_h > 2 * _fullsize + 2 * _shift) ? (_h - _fullsize) : (_fullsize + 2 * _shift);
		int to = h - _fullsize;
		if (h > _heightMax) {
			from = (_fullsize + 2 * _shift);
			do {
				_heightMax *= 2;
			} while (h > _heightMax);
			for (int i = 1; i < 4; i += 2) {
				DeleteObject(_bitmaps[i]);
				_bitmaps[i] = CreateCompatibleBitmap(_contexts[i], _size, _heightMax);
				SelectObject(_contexts[i], _bitmaps[i]);
			}
			initCorners(Direction::Vertical);
		}
		Gdiplus::Graphics graphics1(_contexts[1]), graphics3(_contexts[3]);
		graphics1.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		graphics3.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);

		auto brush = getBrush(_alphas[0]);
		if (to > from) {
			graphics1.FillRectangle(&brush, _size - _shift, from, _shift, to - from);
			graphics3.FillRectangle(&brush, 0, from, _shift, to - from);
			for (int i = 2 * _shift; i < _size + _shift; ++i) {
				auto pen = getPen(_alphas[i]);
				graphics1.DrawLine(&pen, _size + _shift - i - 1, from, _size + _shift - i - 1, to);
				graphics3.DrawLine(&pen, i - _shift, from, i - _shift, to);
			}
		}
		if (_h > h) {
			graphics1.FillRectangle(&brush, 0, h - _fullsize, _size, _fullsize);
			graphics3.FillRectangle(&brush, 0, h - _fullsize, _size, _fullsize);
		}
		verCorners(h, &graphics1, &graphics3);

		POINT p1 = { x + w - _size, y }, p3 = { x, y }, f = { 0, 0 };
		SIZE s = { _size, h };
		updateWindow(1, &p1, &s);
		updateWindow(3, &p3, &s);
	} else if (x != _x || y != _y) {
		POINT p1 = { x + w - _size, y }, p3 = { x, y };
		updateWindow(1, &p1);
		updateWindow(3, &p3);
	} else if (w != _w) {
		POINT p1 = { x + w - _size, y };
		updateWindow(1, &p1);
	}
	_x = x;
	_y = y;
	_w = w;
	_h = h;

	if (_hidden && (changes & Change::Shown)) {
		for (int i = 0; i < 4; ++i) {
			SetWindowPos(
				_handles[i],
				_handle,
				0,
				0,
				0,
				0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
		}
		_hidden = false;
	}
}

void WindowShadow::initBlend() {
	_blend.AlphaFormat = AC_SRC_ALPHA;
	_blend.SourceConstantAlpha = 255;
	_blend.BlendFlags = 0;
	_blend.BlendOp = AC_SRC_OVER;
}

void WindowShadow::updateWindow(int i, POINT *p, SIZE *s) {
	static POINT f = { 0, 0 };
	if (s) {
		UpdateLayeredWindow(
			_handles[i],
			(s ? _screenContext : nullptr),
			p,
			s,
			(s ? _contexts[i] : nullptr),
			(s ? (&f) : nullptr),
			_noKeyColor,
			&_blend,
			ULW_ALPHA);
	} else {
		SetWindowPos(
			_handles[i],
			0,
			p->x,
			p->y,
			0,
			0,
			SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
	}
}

void WindowShadow::setResizeEnabled(bool enabled) {
	_resizeEnabled = enabled;
}

LRESULT CALLBACK WindowShadow::WindowCallback(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam) {
	const auto i = ShadowByHandle.find(hwnd);
	return (i != end(ShadowByHandle))
		? i->second->windowCallback(hwnd, msg, wParam, lParam)
		: DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT WindowShadow::windowCallback(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam) {
	if (!working()) {
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	switch (msg) {
	case WM_CLOSE:
		_window->close();
		return 0;

	case WM_NCHITTEST: {
		if (!_resizeEnabled) {
			return HTNOWHERE;
		}
		const auto yPos = GET_Y_LPARAM(lParam);
		if (hwnd == _handles[0]) {
			return HTTOP;
		} else if (hwnd == _handles[1]) {
			return (yPos < _y + _size)
				? HTTOPRIGHT
				: (yPos >= _y + _h - _size)
				? HTBOTTOMRIGHT
				: HTRIGHT;
		} else if (hwnd == _handles[2]) {
			return HTBOTTOM;
		} else if (hwnd == _handles[3]) {
			return (yPos < _y + _size)
				? HTTOPLEFT
				: (yPos >= _y + _h - _size)
				? HTBOTTOMLEFT
				: HTLEFT;
		} else {
			Unexpected("Handle in WindowShadow::windowCallback.");
		}
	} break;

	case WM_NCACTIVATE:
		return DefWindowProc(hwnd, msg, wParam, lParam);

	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONUP:
	case WM_NCLBUTTONDBLCLK:
	case WM_NCMBUTTONDOWN:
	case WM_NCMBUTTONUP:
	case WM_NCMBUTTONDBLCLK:
	case WM_NCRBUTTONDOWN:
	case WM_NCRBUTTONUP:
	case WM_NCRBUTTONDBLCLK:
	case WM_NCXBUTTONDOWN:
	case WM_NCXBUTTONUP:
	case WM_NCXBUTTONDBLCLK:
	case WM_NCMOUSEHOVER:
	case WM_NCMOUSELEAVE:
	case WM_NCMOUSEMOVE:
	case WM_NCPOINTERUPDATE:
	case WM_NCPOINTERDOWN:
	case WM_NCPOINTERUP:
		if (msg == WM_NCLBUTTONDOWN) {
			::SetForegroundWindow(_handle);
		}
		return SendMessage(_handle, msg, wParam, lParam);
	case WM_ACTIVATE:
		if (wParam == WA_ACTIVE) {
			if ((HWND)lParam != _handle) {
				::SetForegroundWindow(hwnd);
				::SetWindowPos(
					_handle,
					hwnd,
					0,
					0,
					0,
					0,
					SWP_NOMOVE | SWP_NOSIZE);
			}
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}

} // namespace Platform
} // namespace Ui
