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

void GenerateTextures(
		QOpenGLFunctions &f,
		gsl::span<GLuint> values,
		GLint filter,
		GLint clamp) {
	Expects(!values.empty());

	f.glGenTextures(values.size(), values.data());

	for (const auto texture : values) {
		f.glBindTexture(GL_TEXTURE_2D, texture);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		f.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	}
}

void DestroyTextures(QOpenGLFunctions &f, gsl::span<GLuint> values) {
	Expects(!values.empty());

	f.glDeleteTextures(values.size(), values.data());
	ranges::fill(values, 0);
}

void GenerateFramebuffers(QOpenGLFunctions &f, gsl::span<GLuint> values) {
	Expects(!values.empty());

	f.glGenFramebuffers(values.size(), values.data());
}

void DestroyFramebuffers(QOpenGLFunctions &f, gsl::span<GLuint> values) {
	Expects(!values.empty());

	f.glDeleteTextures(values.size(), values.data());
	ranges::fill(values, 0);
}

} // namespace details

void Image::setImage(QImage image, QSize subimage) {
	Expects(subimage.width() <= image.width()
		&& subimage.height() <= image.height());

	_image = std::move(image);
	_subimage = subimage.isValid() ? subimage : _image.size();
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
	_textures.ensureCreated(f, GL_NEAREST);
	if (_subimage.isEmpty()) {
		_textureSize = _subimage;
		return;
	}
	const auto cacheKey = _image.cacheKey();
	const auto upload = (_cacheKey != cacheKey);
	if (upload) {
		_cacheKey = cacheKey;
	}
	_textures.bind(f, 0);
	if (upload) {
		f.glPixelStorei(GL_UNPACK_ROW_LENGTH, _image.bytesPerLine() / 4);
		if (_textureSize.width() < _subimage.width()
			|| _textureSize.height() < _subimage.height()) {
			_textureSize = _subimage;
			f.glTexImage2D(
				GL_TEXTURE_2D,
				0,
				kFormatRGBA,
				_subimage.width(),
				_subimage.height(),
				0,
				kFormatRGBA,
				GL_UNSIGNED_BYTE,
				_image.constBits());
		} else {
			f.glTexSubImage2D(
				GL_TEXTURE_2D,
				0,
				0,
				0,
				_subimage.width(),
				_subimage.height(),
				kFormatRGBA,
				GL_UNSIGNED_BYTE,
				_image.constBits());
		}
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
	if (visible.isEmpty()) {
		return TexturedRect{
			.geometry = Rect(visible),
			.texture = Rect(0., 0., 0., 0.),
		};
	}
	const auto xFactor = texture.width() / geometry.width();
	const auto yFactor = texture.height() / geometry.height();
	const auto usedTexture = QRect(
		texture.x() + (visible.x() - geometry.x()) * xFactor,
		texture.y() + (visible.y() - geometry.y()) * yFactor,
		visible.width() * xFactor,
		visible.height() * yFactor);
	const auto dimensions = QSizeF((_textureSize.width() < _subimage.width()
		|| _textureSize.height() < _subimage.height())
		? _subimage
		: _textureSize);
	return {
		.geometry = Rect(visible),
		.texture = Rect(QRectF(
			usedTexture.x() / dimensions.width(),
			usedTexture.y() / dimensions.height(),
			usedTexture.width() / dimensions.width(),
			usedTexture.height() / dimensions.height())),
	};
}

GLint CurrentSingleComponentFormat() {
	const auto context = QOpenGLContext::currentContext();
	Assert(context != nullptr);

	const auto format = context->format();
	return (format.renderableType() == QSurfaceFormat::OpenGLES)
		? GL_LUMINANCE
		: GL_RED;
}

} // namespace Ui::GL
