// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/rhi/rhi_image.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <rhi/qrhi.h>
#endif // Qt >= 6.7

namespace Ui::Rhi {

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
	_subimage = QSize();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void Image::ensureCreated(QRhi *rhi) {
	if (_texture && _rhi == rhi && _textureSize == _subimage) {
		return;
	}
	delete _sampler;
	_sampler = nullptr;
	delete _texture;
	_texture = nullptr;
	_rhi = rhi;
	_texture = rhi->newTexture(
		QRhiTexture::BGRA8,
		_subimage,
		1,
		{});
	_texture->create();
	_sampler = rhi->newSampler(
		QRhiSampler::Nearest,
		QRhiSampler::Nearest,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	_sampler->create();
	_textureSize = _subimage;
}

void Image::upload(QRhi *rhi, QRhiResourceUpdateBatch *rub) {
	if (_subimage.isEmpty()) {
		_textureSize = _subimage;
		return;
	}
	const auto cacheKey = _image.cacheKey();
	const auto needsUpload = (_cacheKey != cacheKey);
	if (!needsUpload) {
		return;
	}
	_cacheKey = cacheKey;

	const auto needsRecreate = !_texture
		|| (_rhi != rhi)
		|| (_textureSize != _subimage);

	if (needsRecreate) {
		ensureCreated(rhi);
	}

	const auto uploadImage = (_subimage == _image.size())
		? _image
		: _image.copy(QRect(QPoint(), _subimage));
	rub->uploadTexture(
		_texture,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(uploadImage))));
}

void Image::destroy() {
	invalidate();
	delete _sampler;
	_sampler = nullptr;
	delete _texture;
	_texture = nullptr;
	_rhi = nullptr;
	_cacheKey = 0;
	_textureSize = QSize();
}

QRhiTexture *Image::texture() const {
	return _texture;
}

QRhiSampler *Image::sampler() const {
	return _sampler;
}
#else // Qt >= 6.7
void Image::upload(QRhi *, QRhiResourceUpdateBatch *) {
}

void Image::destroy() {
	invalidate();
	_cacheKey = 0;
	_textureSize = QSize();
}

QRhiTexture *Image::texture() const {
	return nullptr;
}

QRhiSampler *Image::sampler() const {
	return nullptr;
}

void Image::ensureCreated(QRhi *) {
}
#endif // Qt >= 6.7

GL::TexturedRect Image::texturedRect(
		const QRect &geometry,
		const QRect &texture,
		const QRect &clip) {
	if (_image.isNull()) {
		return GL::TexturedRect{
			.geometry = GL::Rect(geometry),
			.texture = GL::Rect(0., 0., 1., 1.),
		};
	}

	const auto visible = clip.isNull()
		? geometry
		: clip.intersected(geometry);
	if (visible.isEmpty()) {
		return GL::TexturedRect{
			.geometry = GL::Rect(visible),
			.texture = GL::Rect(0., 0., 0., 0.),
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
		.geometry = GL::Rect(visible),
		.texture = GL::Rect(QRectF(
			usedTexture.x() / dimensions.width(),
			usedTexture.y() / dimensions.height(),
			usedTexture.width() / dimensions.width(),
			usedTexture.height() / dimensions.height())),
	};
}

} // namespace Ui::Rhi
