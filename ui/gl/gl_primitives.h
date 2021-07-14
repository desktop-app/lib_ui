// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/gl/gl_math.h"
#include "ui/style/style_core.h"

#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLShaderProgram>

class QOpenGLFunctions;

namespace Ui::GL {

void FillRectTriangleVertices(float *coords, Rect rect);
void FillTriangles(
	QOpenGLFunctions &f,
	gsl::span<const float> coords,
	not_null<QOpenGLBuffer*> buffer,
	not_null<QOpenGLShaderProgram*> program,
	const QColor &color,
	Fn<void()> additional = nullptr);

void FillRectangle(
	QOpenGLFunctions &f,
	not_null<QOpenGLShaderProgram*> program,
	int skipVertices,
	const QColor &color);

void FillTexturedRectangle(
	QOpenGLFunctions &f,
	not_null<QOpenGLShaderProgram*> program,
	int skipVertices = 0);

} // namespace Ui::GL
