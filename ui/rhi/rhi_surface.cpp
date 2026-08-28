// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/rhi/rhi_surface.h"

#include "ui/rhi/rhi_renderer.h"
#include "ui/rp_widget.h"
#include "ui/qt_object_factory.h"
#include "ui/painter.h"
#include "base/debug_log.h"
#include "base/platform/base_platform_info.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QRhiWidget>
#include <QBackingStore>
#include <QtGui/QWindow>
#include <qpa/qplatformbackingstore.h>
#include <rhi/qrhi.h>
#endif // Qt >= 6.7

#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#include <commctrl.h>
#endif // Q_OS_WIN

namespace Ui::GL {

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
namespace {

struct SurfaceRhiTraits : RpWidgetDefaultTraits {
	static constexpr bool kSetZeroGeometry = false;
};

void ApplyRhiApi(QRhiWidget *widget) {
	if (WidgetsRhiVulkan()) {
		widget->setApi(QRhiWidget::Api::Vulkan);
		return;
	}
#ifdef Q_OS_MAC
	if (!::Platform::MetalSupported()) {
		widget->setApi(QRhiWidget::Api::OpenGL);
	}
#endif
}

} // namespace

class SurfaceRhi final
	: public RpWidgetBase<QRhiWidget, SurfaceRhiTraits> {
public:
	SurfaceRhi(QWidget *parent, std::unique_ptr<Renderer> renderer);
	~SurfaceRhi();

protected:
	void initialize(QRhiCommandBuffer *cb) override;
	void render(QRhiCommandBuffer *cb) override;
	void releaseResources() override;

private:
	[[nodiscard]] Rhi::Renderer *rhiRenderer() const;
#ifdef Q_OS_WIN
	void installExStyleFilterWin();
	void removeExStyleFilterWin();
#endif // Q_OS_WIN

	const std::unique_ptr<Renderer> _renderer;
#ifdef Q_OS_WIN
	HWND _exStyleFilterHwnd = nullptr;
#endif // Q_OS_WIN

};

SurfaceRhi::SurfaceRhi(
	QWidget *parent,
	std::unique_ptr<Renderer> renderer)
: _renderer(std::move(renderer)) {
	ApplyRhiApi(this);
	setParent(parent);
	LOG(("QRhi: SurfaceRhi created"));
}

SurfaceRhi::~SurfaceRhi() {
	// Call releaseResources() here in the destructor body, BEFORE
	// member destruction begins. At this point QRhiWidget's QRhi is
	// still alive (base destructor hasn't run yet), so QRhi resource
	// deletion is safe. This handles the deleteChildren() teardown
	// path where Qt doesn't call releaseResources() automatically.
	releaseResources();
#ifdef Q_OS_WIN
	removeExStyleFilterWin();
#endif // Q_OS_WIN
}

#ifdef Q_OS_WIN
namespace {

constexpr UINT_PTR kExStyleSubclassId = 0x51F4C4FA;

LRESULT CALLBACK StripLayeredExStyleSubclass(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		UINT_PTR uIdSubclass,
		DWORD_PTR /*dwRefData*/) {
	// WS_EX_LAYERED breaks DirectComposition output: the target and visual
	// are created fine, but nothing composites through the swap chain.
	if (msg == WM_STYLECHANGING && wParam == GWL_EXSTYLE && lParam) {
		auto *ss = reinterpret_cast<STYLESTRUCT*>(lParam);
		ss->styleNew &= ~LONG(WS_EX_LAYERED);
	} else if (msg == WM_NCDESTROY) {
		::RemoveWindowSubclass(
			hwnd,
			&StripLayeredExStyleSubclass,
			uIdSubclass);
	}
	return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

void SurfaceRhi::installExStyleFilterWin() {
	if (_exStyleFilterHwnd) {
		return;
	} else if (!::Platform::IsWindows8OrGreater()) {
		// DirectComposition is Windows 8+, so on Windows 7 WS_EX_LAYERED
		// stays the only way a translucent top-level window composites.
		LOG(("QRhi: Windows 7, keeping WS_EX_LAYERED."));
		return;
	}
	const auto tlw = window();
	if (!tlw || !tlw->testAttribute(Qt::WA_TranslucentBackground)) {
		LOG(("QRhi: Not translucent, no ex-style filter."));
		return;
	}
	const auto handle = tlw->windowHandle();
	if (!handle || handle->surfaceType() != QSurface::Direct3DSurface) {
		LOG(("QRhi: Surface type %1, no ex-style filter."
			).arg(handle ? int(handle->surfaceType()) : -1));
		return;
	}
	const auto hwnd = reinterpret_cast<HWND>(handle->winId());
	if (!hwnd
		|| !::SetWindowSubclass(
			hwnd,
			&StripLayeredExStyleSubclass,
			kExStyleSubclassId,
			0)) {
		return;
	}
	_exStyleFilterHwnd = hwnd;
	// Qt may have set the bit before the subclass was attached.
	const auto exStyle = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	LOG(("QRhi: Ex-style filter installed, WS_EX_LAYERED was %1."
		).arg((exStyle & WS_EX_LAYERED) ? u"set"_q : u"clear"_q));
	if (exStyle & WS_EX_LAYERED) {
		::SetWindowLongPtrW(
			hwnd,
			GWL_EXSTYLE,
			exStyle & ~LONG_PTR(WS_EX_LAYERED));
	}
}

void SurfaceRhi::removeExStyleFilterWin() {
	if (!_exStyleFilterHwnd) {
		return;
	}
	::RemoveWindowSubclass(
		_exStyleFilterHwnd,
		&StripLayeredExStyleSubclass,
		kExStyleSubclassId);
	_exStyleFilterHwnd = nullptr;
}
#endif // Q_OS_WIN

void SurfaceRhi::initialize(QRhiCommandBuffer *cb) {
#ifdef Q_OS_WIN
	installExStyleFilterWin();
#endif // Q_OS_WIN
	if (const auto use = rhi()) {
		[[maybe_unused]] static const auto logged = [&] {
			LOG(("QRhi: Surface backend=%1 device=%2."
				).arg(use->backendName()
				).arg(use->driverInfo().deviceName));
			return true;
		}();
	}
	if (const auto r = rhiRenderer()) {
		r->initialize(rhi(), renderTarget(), cb);
	}
}

void SurfaceRhi::render(QRhiCommandBuffer *cb) {
	if (!updatesEnabled() || size().isEmpty()) {
		return;
	}
	if (const auto r = rhiRenderer()) {
		r->render(rhi(), renderTarget(), cb);
	}
}

void SurfaceRhi::releaseResources() {
	if (const auto r = rhiRenderer()) {
		r->releaseResources();
	}
}

Rhi::Renderer *SurfaceRhi::rhiRenderer() const {
	return dynamic_cast<Rhi::Renderer*>(_renderer.get());
}
#endif // Qt >= 6.7

std::unique_ptr<RpWidgetWrap> CreateSurfaceRhi(
		QWidget *parent,
		std::unique_ptr<Renderer> renderer) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	return std::make_unique<SurfaceRhi>(
		parent,
		std::move(renderer));
#else // Qt >= 6.7
	LOG(("QRhi: Not available (Qt < 6.7), falling back to raster."));
	return nullptr;
#endif // Qt >= 6.7
}

void EnsureWindowRhi(not_null<QWidget*> window) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (!WidgetsRhiSupported()) {
		return;
	}
	const auto primer = Ui::CreateChild<QRhiWidget>(window.get());
	ApplyRhiApi(primer);
	primer->setAttribute(Qt::WA_TransparentForMouseEvents);
	primer->setGeometry(0, 0, 1, 1);
	primer->hide();
	LOG(("QRhi: backing store primed for window"));
#endif // Qt >= 6.7
}

bool WindowUsesRhi(not_null<QWidget*> widget) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	const auto window = widget->window();
	const auto handle = window->windowHandle();
	const auto store = window->backingStore();
	const auto platform = store ? store->handle() : nullptr;
	return handle && platform && (platform->rhi(handle) != nullptr);
#else // Qt >= 6.7
	return false;
#endif // Qt >= 6.7
}

void LogWindowRhi(const char *tag, not_null<QWidget*> widget) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	const auto window = widget->window();
	const auto handle = window->windowHandle();
	if (!handle) {
		LOG(("QRhi-Check [%1]: window not realized yet.").arg(tag));
		return;
	}
	const auto surface = [&]() -> QString {
		switch (handle->surfaceType()) {
		case QSurface::RasterSurface: return u"Raster"_q;
		case QSurface::OpenGLSurface: return u"OpenGL"_q;
		case QSurface::RasterGLSurface: return u"RasterGL"_q;
		case QSurface::VulkanSurface: return u"Vulkan"_q;
		case QSurface::MetalSurface: return u"Metal"_q;
		case QSurface::Direct3DSurface: return u"Direct3D"_q;
		}
		return QString::number(int(handle->surfaceType()));
	}();
	LOG(("QRhi-Check [%1]: surface=%2, usesRhi=%3."
		).arg(tag
		).arg(surface
		).arg(WindowUsesRhi(widget) ? u"YES"_q : u"no"_q));
#endif // Qt >= 6.7
}

} // namespace Ui::GL
