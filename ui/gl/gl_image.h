// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/gl/gl_math.h"

#include <QtGui/QOpenGLFunctions>

namespace Ui::GL {
namespace details {

void GenerateTextures(QOpenGLFunctions &f, gsl::span<GLuint> values);
void DestroyTextures(QOpenGLFunctions &f, gsl::span<GLuint> values);

} // namespace details

template <size_t Count>
class Textures final {
public:
	static_assert(Count > 0);

	void ensureCreated(QOpenGLFunctions &f) {
		if (!created()) {
			details::GenerateTextures(f, gsl::make_span(_values));
		}
	}
	void destroy(QOpenGLFunctions &f) {
		if (created()) {
			details::DestroyTextures(f, gsl::make_span(_values));
		}
	}

	[[nodiscard]] void bind(QOpenGLFunctions &f, int index) const {
		Expects(index >= 0 && index < Count);

		f.glBindTexture(GL_TEXTURE_2D, _values[index]);
	}

	[[nodiscard]] bool created() const {
		return (_values[0] != 0);
	}

private:
	std::array<GLuint, Count> _values = { { 0 } };

};

struct TexturedRect {
	Rect geometry;
	Rect texture;
};

class Image final {
public:
	void setImage(QImage image);
	[[nodiscard]] const QImage &image() const;
	[[nodiscard]] QImage takeImage();
	void invalidate();

	void bind(QOpenGLFunctions &f);
	void destroy(QOpenGLFunctions &f);

	[[nodiscard]] TexturedRect texturedRect(
		const QRect &geometry,
		const QRect &texture,
		const QRect &clip = QRect());

	explicit operator bool() const {
		return !_image.isNull();
	}

private:
	QImage _image;
	QImage _storage;
	Textures<2> _textures;
	qint64 _cacheKey = 0;
	int _index = 0;

};

} // namespace Ui::GL
