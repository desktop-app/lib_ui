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
#endif // Qt >= 6.7

namespace Ui::GL {

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
namespace {

struct SurfaceRhiTraits : RpWidgetDefaultTraits {
	static constexpr bool kSetZeroGeometry = false;
};

void ApplyRhiApi(QRhiWidget *widget) {
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

	const std::unique_ptr<Renderer> _renderer;

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
}

void SurfaceRhi::initialize(QRhiCommandBuffer *cb) {
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
