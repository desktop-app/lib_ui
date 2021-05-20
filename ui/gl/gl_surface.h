// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/gl/gl_detection.h"

class Painter;
class QOpenGLWidget;
class QOpenGLFunctions;

namespace Ui {
class RpWidgetWrap;
} // namespace Ui

namespace Ui::GL {

enum class Backend {
    OpenGL,
    Raster,
};

class Renderer {
public:
    virtual void init(
        not_null<QOpenGLWidget*> widget,
        not_null<QOpenGLFunctions*> f) {
    }

    virtual void resize(
        not_null<QOpenGLWidget*> widget,
        not_null<QOpenGLFunctions*> f,
        int w,
        int h) {
    }

    virtual void paint(
        not_null<QOpenGLWidget*> widget,
        not_null<QOpenGLFunctions*> f);

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
    QWidget *parent,
    Fn<ChosenRenderer(Capabilities)> chooseRenderer);

} // namespace Ui::GL
