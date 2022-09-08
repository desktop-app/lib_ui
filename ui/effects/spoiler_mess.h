// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <crl/crl_time.h>

namespace Ui {

struct SpoilerMessDescriptor {
	crl::time particleFadeInDuration = 0;
	crl::time particleShownDuration = 0;
	crl::time particleFadeOutDuration = 0;
	float64 particleSizeMin = 0.;
	float64 particleSizeMax = 0.;
	int particleSpritesCount = 0;
	int particlesCount = 0;
	int canvasSize = 0;
	int framesCount = 0;
	crl::time frameDuration = 0;
};

struct SpoilerMessFrame {
	not_null<const QImage*> image;
	QRect source;
};

class SpoilerMessCached final {
public:
	SpoilerMessCached(
		QImage image,
		int framesCount,
		crl::time frameDuration,
		int size);

	[[nodiscard]] SpoilerMessFrame frame(int index) const;
	[[nodiscard]] SpoilerMessFrame frame() const; // Current by time.

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] static std::optional<SpoilerMessCached> FromSerialized(
		const QByteArray &data);

private:
	QImage _image;
	crl::time _frameDuration = 0;
	int _framesCount = 0;
	int _canvasSize = 0;

};

[[nodiscard]] SpoilerMessCached GenerateSpoilerMess(
	const SpoilerMessDescriptor &descriptor);

} // namespace Ui
