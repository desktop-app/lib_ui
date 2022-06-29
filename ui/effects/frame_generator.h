// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QImage>

#include <crl/crl_time.h>

namespace Ui {

class FrameGenerator {
public:
	[[nodiscard]] virtual int count() = 0;

	struct Frame {
		QImage image;
		crl::time duration = 0;
	};
	[[nodiscard]] virtual Frame renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) = 0;
};

class ImageFrameGenerator final : public Ui::FrameGenerator {
public:
	explicit ImageFrameGenerator(const QByteArray &bytes);

	int count() override;
	Frame renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) override;

private:
	QByteArray _bytes;

};

} // namespace Ui
