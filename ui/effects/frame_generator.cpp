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

int ImageFrameGenerator::count() {
	return 1;
}

FrameGenerator::Frame ImageFrameGenerator::renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode) {
	storage = Images::Read({
		.content = _bytes,
	}).image;
	if (storage.isNull()) {
		return {};
	}
	auto scaled = storage.scaled(
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
	const auto fromPerLine = scaled.bytesPerLine();
	const auto toPerLine = result.bytesPerLine();
	const auto lineBytes = scaled.width() * 4;
	auto from = scaled.constBits();
	auto to = result.bits() + (skipx * 4) + (skipy * fromPerLine);
	for (auto y = 0, height = scaled.height(); y != height; ++y) {
		memcpy(to, from, lineBytes);
		from += fromPerLine;
		to += toPerLine;
	}

	return { .image = std::move(result) };
}

} // namespace Ui
