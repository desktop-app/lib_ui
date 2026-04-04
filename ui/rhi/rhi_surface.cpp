// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/rhi/rhi_surface.h"

#include "ui/rhi/rhi_renderer.h"
#include "ui/rp_widget.h"
#include "ui/painter.h"
#include "base/debug_log.h"
#include "base/platform/base_platform_info.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QRhiWidget>
#include <QBackingStore>
#include <rhi/qrhi.h>
#include <QtGui/QWindow>
#include <qpa/qplatformbackingstore.h>
#endif // Qt >= 6.7

namespace Ui::GL {

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
namespace {

struct SurfaceRhiTraits : RpWidgetDefaultTraits {
	static constexpr bool kSetZeroGeometry = false;
};

} // namespace

class SurfaceRhi final
	: public RpWidgetBase<QRhiWidget, SurfaceRhiTraits> {
public:
	SurfaceRhi(QWidget *parent, std::unique_ptr<Renderer> renderer);

protected:
	void initialize(QRhiCommandBuffer *cb) override;
	void render(QRhiCommandBuffer *cb) override;
	void releaseResources() override;
	bool eventHook(QEvent *e) override;

private:
	[[nodiscard]] Rhi::Renderer *rhiRenderer() const;
	void ensureBackingStoreRhi();

	const std::unique_ptr<Renderer> _renderer;
	bool _backingStoreConfigured = false;

};

SurfaceRhi::SurfaceRhi(
	QWidget *parent,
	std::unique_ptr<Renderer> renderer)
: RpWidgetBase<QRhiWidget, SurfaceRhiTraits>(parent)
, _renderer(std::move(renderer)) {
#ifdef Q_OS_MAC
	setApi(::Platform::MetalSupported()
		? QRhiWidget::Api::Metal
		: QRhiWidget::Api::OpenGL);
#elif defined(Q_OS_WIN)
	setApi(QRhiWidget::Api::Direct3D11);
#else
	setApi(QRhiWidget::Api::OpenGL);
#endif
	LOG(("QRhi: SurfaceRhi created"));
}

void SurfaceRhi::ensureBackingStoreRhi() {
	if (_backingStoreConfigured) {
		return;
	}
	_backingStoreConfigured = true;

	const auto tlw = window();
	if (!tlw) {
		return;
	}
	const auto wh = tlw->windowHandle();
	if (!wh) {
		return;
	}
	auto *bs = tlw->backingStore();
	if (!bs) {
		return;
	}
	auto *handle = bs->handle();
	if (!handle) {
		return;
	}
	QPlatformBackingStoreRhiConfig config;
	config.setEnabled(true);
#ifdef Q_OS_MAC
	if (::Platform::MetalSupported()) {
		config.setApi(QPlatformBackingStoreRhiConfig::Metal);
		if (wh->surfaceType() != QSurface::MetalSurface) {
			wh->setSurfaceType(QSurface::MetalSurface);
		}
	} else {
		config.setApi(QPlatformBackingStoreRhiConfig::OpenGL);
	}
#elif defined(Q_OS_WIN)
	config.setApi(QPlatformBackingStoreRhiConfig::D3D11);
#else
	config.setApi(QPlatformBackingStoreRhiConfig::OpenGL);
#endif
	LOG(("QRhi: Configuring backing store RHI for window"));
	handle->createRhi(wh, config);
}

bool SurfaceRhi::eventHook(QEvent *e) {
	if (e->type() == QEvent::Show
		|| e->type() == QEvent::Paint
		|| e->type() == QEvent::Resize) {
		ensureBackingStoreRhi();
	}
	return RpWidgetBase<QRhiWidget, SurfaceRhiTraits>::eventHook(e);
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

} // namespace Ui::GL
