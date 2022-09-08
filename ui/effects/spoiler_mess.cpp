// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/spoiler_mess.h"

#include "ui/painter.h"
#include "base/random.h"

namespace Ui {
namespace {

constexpr auto kFramesPerRow = 10;

struct Particle {
	crl::time start = 0;
	int spriteIndex = 0;
	int x = 0;
	int y = 0;
};

[[nodiscard]] Particle GenerateParticle(
		const SpoilerMessDescriptor &descriptor,
		int index,
		base::BufferedRandom<uint32> &random) {
	return {
		.start = (index * descriptor.framesCount * descriptor.frameDuration
			/ descriptor.particlesCount),
		.spriteIndex = RandomIndex(descriptor.particleSpritesCount, random),
		.x = RandomIndex(descriptor.canvasSize, random),
		.y = RandomIndex(descriptor.canvasSize, random),
	};
}

[[nodiscard]] QImage GenerateSprite(
		const SpoilerMessDescriptor &descriptor,
		int index,
		int size,
		base::BufferedRandom<uint32> &random) {
	Expects(index >= 0 && index < descriptor.particleSpritesCount);

	const auto count = descriptor.particleSpritesCount;
	const auto middle = count / 2;
	const auto min = descriptor.particleSizeMin;
	const auto delta = descriptor.particleSizeMax - min;
	const auto width = (index < middle)
		? (min + delta * (middle - index) / float64(middle))
		: min;
	const auto height = (index > middle)
		? (min + delta * (index - middle) / float64(count - 1 - middle))
		: min;
	const auto radius = min / 2.;

	auto result = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.drawRoundedRect(1., 1., width, height, radius, radius);
	p.end();
	return result;
}

} // namespace

SpoilerMessCached GenerateSpoilerMess(
		const SpoilerMessDescriptor &descriptor) {
	Expects(descriptor.framesCount > 0);
	Expects(descriptor.frameDuration > 0);
	Expects(descriptor.particlesCount > 0);
	Expects(descriptor.canvasSize > 0);
	Expects(descriptor.particleSizeMax >= descriptor.particleSizeMin);
	Expects(descriptor.particleSizeMin > 0.);

	const auto frames = descriptor.framesCount;
	const auto rows = (frames + kFramesPerRow - 1) / kFramesPerRow;
	const auto columns = std::min(frames, kFramesPerRow);
	const auto size = descriptor.canvasSize;
	const auto count = descriptor.particlesCount;
	const auto width = size * columns;
	const auto height = size * rows;
	const auto spriteSize = 2 + int(std::ceil(descriptor.particleSizeMax));
	const auto singleDuration = descriptor.particleFadeInDuration
		+ descriptor.particleShownDuration
		+ descriptor.particleFadeOutDuration;
	const auto fullDuration = frames * descriptor.frameDuration;
	Assert(fullDuration > singleDuration);

	auto random = base::BufferedRandom<uint32>(count * 3);

	auto particles = std::vector<Particle>();
	particles.reserve(descriptor.particlesCount);
	for (auto i = 0; i != descriptor.particlesCount; ++i) {
		particles.push_back(GenerateParticle(descriptor, i, random));
	}

	auto sprites = std::vector<QImage>();
	sprites.reserve(descriptor.particleSpritesCount);
	for (auto i = 0; i != descriptor.particleSpritesCount; ++i) {
		sprites.push_back(GenerateSprite(descriptor, i, spriteSize, random));
	}

	auto frame = 0;
	auto image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	const auto paintOneAt = [&](const Particle &particle, crl::time time) {
		if (time <= 0 || time >= singleDuration) {
			return;
		}
		const auto opacity = (time < descriptor.particleFadeInDuration)
			? (time / float64(descriptor.particleFadeInDuration))
			: (time > singleDuration - descriptor.particleFadeOutDuration)
			? ((singleDuration - time)
				/ float64(descriptor.particleFadeOutDuration))
			: 1.;
		p.setOpacity(opacity);
		const auto &sprite = sprites[particle.spriteIndex];
		p.drawImage(particle.x, particle.y, sprite);
		if (particle.x + spriteSize > size) {
			p.drawImage(particle.x - size, particle.y, sprite);
			if (particle.y + spriteSize > size) {
				p.drawImage(particle.x, particle.y - size, sprite);
				p.drawImage(particle.x - size, particle.y - size, sprite);
			}
		} else if (particle.y + spriteSize > size) {
			p.drawImage(particle.x, particle.y - size, sprite);
		}
	};
	const auto paintOne = [&](const Particle &particle, crl::time now) {
		paintOneAt(particle, now - particle.start);
		paintOneAt(particle, now + fullDuration - particle.start);
	};
	for (auto y = 0; y != rows; ++y) {
		for (auto x = 0; x != columns; ++x) {
			const auto rect = QRect(x * size, y * size, size, size);
			p.setClipRect(rect);
			p.translate(rect.topLeft());
			const auto time = frame * descriptor.frameDuration;
			for (auto index = 0; index != count; ++index) {
				paintOne(particles[index], time);
			}
			p.translate(-rect.topLeft());
			if (++frame >= frames) {
				break;
			}
		}
	}
	return SpoilerMessCached(
		std::move(image),
		frames,
		descriptor.frameDuration,
		size);
}

SpoilerMessCached::SpoilerMessCached(
	QImage image,
	int framesCount,
	crl::time frameDuration,
	int canvasSize)
: _image(std::move(image))
, _frameDuration(frameDuration)
, _framesCount(framesCount)
, _canvasSize(canvasSize) {
	Expects(_frameDuration > 0);
	Expects(_framesCount > 0);
	Expects(_canvasSize > 0);
	Expects(_image.size() == QSize(
		std::min(_framesCount, kFramesPerRow) * _canvasSize,
		((_framesCount + kFramesPerRow - 1) / kFramesPerRow) * _canvasSize));
}

SpoilerMessFrame SpoilerMessCached::frame(int index) const {
	const auto row = index / kFramesPerRow;
	const auto column = index - row * kFramesPerRow;
	return {
		.image = &_image,
		.source = QRect(
			column * _canvasSize,
			row * _canvasSize,
			_canvasSize,
			_canvasSize),
	};
}

SpoilerMessFrame SpoilerMessCached::frame() const {
	return frame((crl::now() / _frameDuration) % _framesCount);
}

QByteArray SpoilerMessCached::serialize() const {
	return QByteArray();
}

std::optional<SpoilerMessCached> SpoilerMessCached::FromSerialized(
		const QByteArray &data) {
	return std::nullopt;
}

} // namespace Ui
