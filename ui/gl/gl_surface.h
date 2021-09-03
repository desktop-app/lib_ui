// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/gl/gl_detection.h"

#include <QtGui/QOpenGLFunctions>

class Painter;
class QOpenGLWidget;

namespace Ui {
class RpWidgetWrap;
} // namespace Ui

namespace Ui::GL {

class Renderer {
public:
    virtual void init(
        not_null<QOpenGLWidget*> widget,
        QOpenGLFunctions &f) {
    }

    virtual void deinit(
        not_null<QOpenGLWidget*> widget,
        QOpenGLFunctions *f) {
    }

    virtual void resize(
        not_null<QOpenGLWidget*> widget,
        QOpenGLFunctions &f,
        int w,
        int h) {
    }

    virtual void paint(
        not_null<QOpenGLWidget*> widget,
        QOpenGLFunctions &f);

	[[nodiscard]] virtual std::optional<QColor> clearColor() {
		return std::nullopt;
	}

    virtual void paintFallback(
        Painter &&p,
        const QRegion &clip,
        Backend backend) {
    }

    virtual ~Renderer() = default;
};

struct ChosenRenderer {
    std::unique_ptr<Renderer> renderer;
    Backend backend = Backend::Raster;
};

[[nodiscard]] std::unique_ptr<RpWidgetWrap> CreateSurface(
    Fn<ChosenRenderer(Capabilities)> chooseRenderer);

[[nodiscard]] std::unique_ptr<RpWidgetWrap> CreateSurface(
	QWidget *parent,
	ChosenRenderer chosen);

} // namespace Ui::GL
