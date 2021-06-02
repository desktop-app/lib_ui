// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_primitives.h"

#include "ui/gl/gl_shader.h"
#include "ui/style/style_core.h"

#include <QtGui/QOpenGLFunctions>

namespace Ui::GL {

static_assert(std::is_same_v<float, GLfloat>);

void FillRectVertices(float *coords, Rect rect) {
	coords[0] = coords[10] = rect.left();
	coords[1] = coords[11] = rect.top();
	coords[2] = rect.right();
	coords[3] = rect.top();
	coords[4] = coords[6] = rect.right();
	coords[5] = coords[7] = rect.bottom();
	coords[8] = rect.left();
	coords[9] = rect.bottom();
}

void FillTriangles(
		QOpenGLFunctions &f,
		gsl::span<const float> coords,
		not_null<QOpenGLBuffer*> buffer,
		not_null<QOpenGLShaderProgram*> program,
		QSize viewportWithFactor,
		const QColor &color,
		Fn<void()> additional) {
	Expects(coords.size() % 6 == 0);

	if (coords.empty()) {
		return;
	}
	buffer->bind();
	buffer->allocate(coords.data(), coords.size() * sizeof(GLfloat));

	f.glUseProgram(program->programId());
	program->setUniformValue("viewport", QSizeF(viewportWithFactor));
	program->setUniformValue("s_color", Uniform(color));

	GLint position = program->attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		nullptr);
	f.glEnableVertexAttribArray(position);

	if (additional) {
		additional();
	}

	f.glDrawArrays(GL_TRIANGLES, 0, coords.size() / 2);

	f.glDisableVertexAttribArray(position);
}

void FillTexturedRectangle(
		QOpenGLFunctions &f,
		not_null<QOpenGLShaderProgram*> program,
		int skipVertices) {
	const auto shift = [&](int elements) {
		return reinterpret_cast<const void*>(
			(skipVertices * 4 + elements) * sizeof(GLfloat));
	};
	GLint position = program->attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(GLfloat),
		shift(0));
	f.glEnableVertexAttribArray(position);

	GLint texcoord = program->attributeLocation("v_texcoordIn");
	f.glVertexAttribPointer(
		texcoord,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(GLfloat),
		shift(2));
	f.glEnableVertexAttribArray(texcoord);

	f.glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	f.glDisableVertexAttribArray(position);
	f.glDisableVertexAttribArray(texcoord);
}

void BackgroundFiller::init(QOpenGLFunctions &f) {
	_bgBuffer.emplace();
	_bgBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_bgBuffer->create();
	_bgProgram.emplace();
	LinkProgram(
		&*_bgProgram,
		VertexShader({ VertexViewportTransform() }),
		FragmentShader({ FragmentStaticColor() }));
}

void BackgroundFiller::deinit(QOpenGLFunctions &f) {
	_bgProgram.reset();
	_bgBuffer.reset();
}

void BackgroundFiller::fill(
		QOpenGLFunctions &f,
		const QRegion &region,
		QSize viewport,
		float factor,
		const style::color &color) {
	const auto &rgb = color->c.toRgb();
	if (region.isEmpty()) {
		return;
	} else if (region.end() - region.begin() == 1
		&& (*region.begin()).size() == viewport) {
		f.glClearColor(rgb.redF(), rgb.greenF(), rgb.blueF(), rgb.alphaF());
		f.glClear(GL_COLOR_BUFFER_BIT);
		return;
	}
	_bgTriangles.resize((region.end() - region.begin()) * 12);
	auto coords = _bgTriangles.data();
	for (const auto rect : region) {
		FillRectVertices(coords, TransformRect(rect, viewport, factor));
		coords += 12;
	}
	FillTriangles(
		f,
		_bgTriangles,
		&*_bgBuffer,
		&*_bgProgram,
		viewport * factor,
		rgb);
}

} // namespace Ui::GL
