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

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QRhiWidget>
#include <rhi/qrhi.h>
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

private:
	[[nodiscard]] Rhi::Renderer *rhiRenderer() const;

	const std::unique_ptr<Renderer> _renderer;

};

SurfaceRhi::SurfaceRhi(
	QWidget *parent,
	std::unique_ptr<Renderer> renderer)
: RpWidgetBase<QRhiWidget, SurfaceRhiTraits>(parent)
, _renderer(std::move(renderer)) {
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
	} else {
		auto p = Painter(this);
		_renderer->paintFallback(p, QRegion(rect()), Backend::QRhi);
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
