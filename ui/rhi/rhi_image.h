// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/gl/gl_image.h"

#include <QtGui/QImage>

class QRhi;
class QRhiTexture;
class QRhiSampler;
class QRhiResourceUpdateBatch;

namespace Ui::Rhi {

class Image final {
public:
	void setImage(QImage image, QSize subimage = QSize());
	[[nodiscard]] const QImage &image() const;
	[[nodiscard]] QImage takeImage();
	void invalidate();

	~Image() { destroy(); }

	void upload(QRhi *rhi, QRhiResourceUpdateBatch *rub);
	void destroy();

	[[nodiscard]] QRhiTexture *texture() const;
	[[nodiscard]] QRhiSampler *sampler() const;

	[[nodiscard]] GL::TexturedRect texturedRect(
		const QRect &geometry,
		const QRect &texture,
		const QRect &clip = QRect());

	explicit operator bool() const {
		return !_image.isNull();
	}

private:
	void ensureCreated(QRhi *rhi);

	QImage _image;
	QImage _storage;
	[[maybe_unused]] QRhiTexture *_texture = nullptr;
	[[maybe_unused]] QRhiSampler *_sampler = nullptr;
	[[maybe_unused]] QRhi *_rhi = nullptr;
	qint64 _cacheKey = 0;
	QSize _subimage;
	QSize _textureSize;

};

} // namespace Ui::Rhi
