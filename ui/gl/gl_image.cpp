// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_image.h"

#include <QtGui/QOpenGLFunctions>

namespace Ui::GL {
namespace details {

void GenerateTextures(QOpenGLFunctions &f, gsl::span<GLuint> values) {
	Expects(!values.empty());

	f.glGenTextures(values.size(), values.data());
	for (const auto texture : values) {
		f.glBindTexture(GL_TEXTURE_2D, texture);
		const auto clamp = GL_CLAMP_TO_EDGE;
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
}

void DestroyTextures(QOpenGLFunctions &f, gsl::span<GLuint> values) {
	Expects(!values.empty());

	f.glDeleteTextures(values.size(), values.data());
	ranges::fill(values, 0);
}

} // namespace details
void Image::setImage(QImage image) {
	_image = std::move(image);
}

const QImage &Image::image() const {
	return _image;
}

QImage Image::takeImage() {
	return _image.isNull() ? base::take(_storage) : base::take(_image);
}

void Image::invalidate() {
	_storage = base::take(_image);
}

void Image::bind(QOpenGLFunctions &f) {
	Expects(!_image.isNull());

	_textures.ensureCreated(f);
	const auto cacheKey = _image.cacheKey();
	const auto upload = (_cacheKey != cacheKey);
	if (upload) {
		_cacheKey = cacheKey;
		_index = 1 - _index;
	}
	_textures.bind(f, _index);
	const auto error = f.glGetError();
	if (upload) {
		f.glPixelStorei(GL_UNPACK_ROW_LENGTH, _image.bytesPerLine() / 4);
		f.glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RGBA,
			_image.width(),
			_image.height(),
			0,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			_image.constBits());
		f.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}

void Image::destroy(QOpenGLFunctions &f) {
	_textures.destroy(f);
	_cacheKey = 0;
}

TexturedRect Image::texturedRect(
		const QRect &geometry,
		const QRect &texture,
		const QRect &clip) {
	Expects(!_image.isNull());

	const auto visible = clip.isNull()
		? geometry
		: clip.intersected(geometry);
	const auto xFactor = texture.width() / geometry.width();
	const auto yFactor = texture.height() / geometry.height();
	const auto usedTexture = QRect(
		texture.x() + (visible.x() - geometry.x()) * xFactor,
		texture.y() + (visible.y() - geometry.y()) * yFactor,
		visible.width() * xFactor,
		visible.height() * yFactor);
	const auto dimensions = QSizeF(_image.size());
	return {
		.geometry = Rect(visible),
		.texture = Rect(QRectF(
			usedTexture.x() / dimensions.width(),
			usedTexture.y() / dimensions.height(),
			usedTexture.width() / dimensions.width(),
			usedTexture.height() / dimensions.height())),
	};
}

} // namespace Ui::GL
