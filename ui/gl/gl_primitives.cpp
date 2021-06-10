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

void FillRectTriangleVertices(float *coords, Rect rect) {
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
		const QColor &color,
		Fn<void()> additional) {
	Expects(coords.size() % 6 == 0);

	if (coords.empty()) {
		return;
	}
	buffer->bind();
	buffer->allocate(coords.data(), coords.size() * sizeof(GLfloat));

	program->setUniformValue("s_color", color);

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

void FillRectangle(
		QOpenGLFunctions &f,
		not_null<QOpenGLShaderProgram*> program,
		int skipVertices,
		const QColor &color) {
	const auto shift = [&](int elements) {
		return reinterpret_cast<const void*>(
			(skipVertices * 4 + elements) * sizeof(GLfloat));
	};
	program->setUniformValue("s_color", color);

	GLint position = program->attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		shift(0));
	f.glEnableVertexAttribArray(position);

	f.glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

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

} // namespace Ui::GL
