// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/frame_generator.h"

#include "ui/image/image_prepare.h"

namespace Ui {

ImageFrameGenerator::ImageFrameGenerator(const QByteArray &bytes)
: _bytes(bytes) {
}

ImageFrameGenerator::ImageFrameGenerator(const QImage &image)
: _image(image) {
}

int ImageFrameGenerator::count() {
	return 1;
}

double ImageFrameGenerator::rate() {
	return 1.;
}

FrameGenerator::Frame ImageFrameGenerator::renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	return renderCurrent(std::move(storage), size, mode);
}

FrameGenerator::Frame ImageFrameGenerator::renderCurrent(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	if (_image.isNull() && !_bytes.isEmpty()) {
		_image = Images::Read({
			.content = _bytes,
		}).image;
		_bytes = QByteArray();
	}
	if (_image.isNull()) {
		return {};
	}
	auto scaled = _image.scaled(
		size,
		mode,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	if (scaled.size() == size) {
		return { .image = std::move(scaled) };
	}
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	const auto skipx = (size.width() - scaled.width()) / 2;
	const auto skipy = (size.height() - scaled.height()) / 2;
	const auto srcPerLine = scaled.bytesPerLine();
	const auto dstPerLine = result.bytesPerLine();
	const auto lineBytes = scaled.width() * 4;
	auto src = scaled.constBits();
	auto dst = result.bits() + (skipx * 4) + (skipy * srcPerLine);
	for (auto y = 0, height = scaled.height(); y != height; ++y) {
		memcpy(dst, src, lineBytes);
		src += srcPerLine;
		dst += dstPerLine;
	}

	return { .image = std::move(result), .last = true };
}

void ImageFrameGenerator::jumpToStart() {
}

} // namespace Ui
