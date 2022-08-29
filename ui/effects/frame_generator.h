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
	virtual ~FrameGenerator() = default;

	// 0 means unknown.
	[[nodiscard]] virtual int count() = 0;

	// 0. means unknown.
	[[nodiscard]] virtual double rate() = 0;

	struct Frame {
		crl::time duration = 0;
		QImage image;
		bool last = false;
	};
	[[nodiscard]] virtual Frame renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) = 0;
	[[nodiscard]] virtual Frame renderCurrent(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) = 0;

	virtual void jumpToStart() = 0;

};

class ImageFrameGenerator final : public Ui::FrameGenerator {
public:
	explicit ImageFrameGenerator(const QByteArray &bytes);
	explicit ImageFrameGenerator(const QImage &image);

	int count() override;
	double rate() override;
	Frame renderNext(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) override;
	Frame renderCurrent(
		QImage storage,
		QSize size,
		Qt::AspectRatioMode mode = Qt::IgnoreAspectRatio) override;
	void jumpToStart() override;

private:
	QByteArray _bytes;
	QImage _image;

};

[[nodiscard]] bool GoodStorageForFrame(const QImage &storage, QSize size);
[[nodiscard]] QImage CreateFrameStorage(QSize size);

} // namespace Ui
