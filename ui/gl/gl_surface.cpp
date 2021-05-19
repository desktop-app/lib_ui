// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_surface.h"

#include "ui/rp_widget.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QOpenGLContext>
#include <QtWidgets/QOpenGLWidget>

namespace Ui::GL {
namespace {

struct SurfaceTraits : RpWidgetDefaultTraits {
	static constexpr bool kSetZeroGeometry = false;
};

class SurfaceOpenGL final
	: public RpWidgetBase<QOpenGLWidget, SurfaceTraits> {
public:
	SurfaceOpenGL(QWidget *parent, std::unique_ptr<Renderer> renderer);

private:
	void initializeGL() override;
	void resizeGL(int w, int h) override;
	void paintGL() override;

	const std::unique_ptr<Renderer> _renderer;

};

class SurfaceRaster final : public RpWidgetBase<QWidget, SurfaceTraits> {
public:
	SurfaceRaster(QWidget *parent, std::unique_ptr<Renderer> renderer);

private:
	void paintEvent(QPaintEvent *e) override;

	const std::unique_ptr<Renderer> _renderer;

};

SurfaceOpenGL::SurfaceOpenGL(
	QWidget *parent,
	std::unique_ptr<Renderer> renderer)
: RpWidgetBase<QOpenGLWidget, SurfaceTraits>(parent)
, _renderer(std::move(renderer)) {
}

void SurfaceOpenGL::initializeGL() {
	_renderer->init(this, context()->functions());
}

void SurfaceOpenGL::resizeGL(int w, int h) {
	_renderer->resize(this, context()->functions(), w, h);
}

void SurfaceOpenGL::paintGL() {
	_renderer->paint(this, context()->functions());
}

SurfaceRaster::SurfaceRaster(
	QWidget *parent,
	std::unique_ptr<Renderer> renderer)
: RpWidgetBase<QWidget, SurfaceTraits>(parent)
, _renderer(std::move(renderer)) {
}

void SurfaceRaster::paintEvent(QPaintEvent *e) {
	_renderer->paintFallback(QPainter(this), e->region(), Backend::Raster);
}

} // namespace

void Renderer::paint(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
	paintFallback(QPainter(widget.get()), widget->rect(), Backend::OpenGL);
}

std::unique_ptr<RpWidgetWrap> CreateSurface(
		QWidget *parent,
		Fn<ChosenRenderer(Capabilities)> chooseRenderer) {
	auto chosen = chooseRenderer(CheckCapabilities(parent));
	switch (chosen.backend) {
	case Backend::OpenGL:
		return std::make_unique<SurfaceOpenGL>(
			parent,
			std::move(chosen.renderer));
	case Backend::Raster:
		return std::make_unique<SurfaceRaster>(
			parent,
			std::move(chosen.renderer));
	}
	Unexpected("Backend value in Ui::GL::CreateSurface.");
}

} // namespace Ui::GL
