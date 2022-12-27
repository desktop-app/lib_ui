// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <crl/crl_time.h>

namespace Images {
struct CornersMaskRef;
} // namespace Images

namespace Ui {

struct SpoilerMessDescriptor {
	crl::time particleFadeInDuration = 0;
	crl::time particleShownDuration = 0;
	crl::time particleFadeOutDuration = 0;
	float64 particleSizeMin = 0.;
	float64 particleSizeMax = 0.;
	float64 particleSpeedMin = 0.;
	float64 particleSpeedMax = 0.;
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

void FillSpoilerRect(
	QPainter &p,
	QRect rect,
	const SpoilerMessFrame &frame,
	QPoint originShift = {});

void FillSpoilerRect(
	QPainter &p,
	QRect rect,
	Images::CornersMaskRef mask,
	const SpoilerMessFrame &frame,
	QImage &cornerCache,
	QPoint originShift = {});

class SpoilerMessCached final {
public:
	SpoilerMessCached(
		QImage image,
		int framesCount,
		crl::time frameDuration,
		int canvasSize);
	SpoilerMessCached(const SpoilerMessCached &mask, const QColor &color);

	[[nodiscard]] SpoilerMessFrame frame(int index) const;
	[[nodiscard]] SpoilerMessFrame frame() const; // Current by time.

	[[nodiscard]] crl::time frameDuration() const;
	[[nodiscard]] int framesCount() const;
	[[nodiscard]] int canvasSize() const;

	struct Validator {
		crl::time frameDuration = 0;
		int framesCount = 0;
		int canvasSize = 0;
	};
	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] static std::optional<SpoilerMessCached> FromSerialized(
		QByteArray data,
		std::optional<Validator> validator = {});

private:
	QImage _image;
	crl::time _frameDuration = 0;
	int _framesCount = 0;
	int _canvasSize = 0;

};

// Works with default frame duration and default frame count.
class SpoilerAnimationManager;
class SpoilerAnimation final {
public:
	explicit SpoilerAnimation(Fn<void()> repaint);
	~SpoilerAnimation();

	[[nodiscard]] int index(crl::time now, bool paused);

	[[nodiscard]] Fn<void()> repaintCallback() const;

private:
	friend class SpoilerAnimationManager;

	[[nodiscard]] bool repaint(crl::time now);

	const Fn<void()> _repaint;
	crl::time _accumulated = 0;
	crl::time _last = 0;
	bool _animating : 1 = false;
	bool _scheduled : 1 = false;

};

[[nodiscard]] SpoilerMessCached GenerateSpoilerMess(
	const SpoilerMessDescriptor &descriptor);

void PreloadTextSpoilerMask();
[[nodiscard]] const SpoilerMessCached &DefaultTextSpoilerMask();
void PreloadImageSpoiler();
[[nodiscard]] const SpoilerMessCached &DefaultImageSpoiler();

} // namespace Ui
