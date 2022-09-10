// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/spoiler_mess.h"

#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/integration.h"
#include "base/random.h"
#include "base/flags.h"

#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtCore/QDir>

#include <crl/crl_async.h>
#include <xxhash.h>
#include <mutex>
#include <condition_variable>

namespace Ui {
namespace {

constexpr auto kVersion = 1;
constexpr auto kFramesPerRow = 10;
constexpr auto kImageSpoilerDarkenAlpha = 32;
constexpr auto kMaxCacheSize = 5 * 1024 * 1024;
constexpr auto kDefaultFrameDuration = crl::time(33);
constexpr auto kDefaultFramesCount = 60;
constexpr auto kDefaultCanvasSize = 128;
constexpr auto kDefaultParticlesCount = 3000;
constexpr auto kDefaultFadeInDuration = crl::time(300);
constexpr auto kDefaultParticleShownDuration = crl::time(0);
constexpr auto kDefaultFadeOutDuration = crl::time(300);

std::atomic<const SpoilerMessCached*> DefaultMask/* = nullptr*/;
std::condition_variable *DefaultMaskSignal/* = nullptr*/;
std::mutex *DefaultMaskMutex/* = nullptr*/;

struct AnimationManager {
	Ui::Animations::Basic animation;
	base::flat_set<not_null<SpoilerAnimation*>> list;
};

AnimationManager *DefaultAnimationManager/* = nullptr*/;

struct Header {
	uint32 version = 0;
	uint32 dataLength = 0;
	uint32 dataHash = 0;
	int32 framesCount = 0;
	int32 canvasSize = 0;
	int32 frameDuration = 0;
};

struct Particle {
	crl::time start = 0;
	int spriteIndex = 0;
	int x = 0;
	int y = 0;
	float64 dx = 0.;
	float64 dy = 0.;
};

[[nodiscard]] std::pair<float64, float64> RandomSpeed(
		const SpoilerMessDescriptor &descriptor,
		base::BufferedRandom<uint32> &random) {
	const auto count = descriptor.particlesCount;
	const auto speedMax = descriptor.particleSpeedMax;
	const auto speedMin = descriptor.particleSpeedMin;
	const auto value = RandomIndex(2 * count + 2, random);
	const auto negative = (value < count + 1);
	const auto module = (negative ? value : (value - count - 1));
	const auto speed = speedMin + (((speedMax - speedMin) * module) / count);
	const auto lifetime = descriptor.particleFadeInDuration
		+ descriptor.particleShownDuration
		+ descriptor.particleFadeOutDuration;
	const auto max = int(std::ceil(speedMax * lifetime));
	const auto k = speed / lifetime;
	const auto x = (RandomIndex(2 * max + 1, random) - max) / float64(max);
	const auto y = sqrt(1 - x * x) * (negative ? -1 : 1);
	return { k * x, k * y };
}

[[nodiscard]] Particle GenerateParticle(
		const SpoilerMessDescriptor &descriptor,
		int index,
		base::BufferedRandom<uint32> &random) {
	const auto speed = RandomSpeed(descriptor, random);
	return {
		.start = (index * descriptor.framesCount * descriptor.frameDuration
			/ descriptor.particlesCount),
		.spriteIndex = RandomIndex(descriptor.particleSpritesCount, random),
		.x = RandomIndex(descriptor.canvasSize, random),
		.y = RandomIndex(descriptor.canvasSize, random),
		.dx = speed.first,
		.dy = speed.second,
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

[[nodiscard]] QString DefaultMaskCacheFolder() {
	const auto base = Integration::Instance().emojiCacheFolder();
	return base.isEmpty() ? QString() : (base + "/spoiler");
}

[[nodiscard]] QString DefaultMaskCachePath(const QString &folder) {
	return folder + "/mask";
}

[[nodiscard]] std::optional<SpoilerMessCached> ReadDefaultMask(
		std::optional<SpoilerMessCached::Validator> validator) {
	const auto folder = DefaultMaskCacheFolder();
	if (folder.isEmpty()) {
		return {};
	}
	auto file = QFile(DefaultMaskCachePath(folder));
	return (file.open(QIODevice::ReadOnly) && file.size() <= kMaxCacheSize)
		? SpoilerMessCached::FromSerialized(file.readAll(), validator)
		: std::nullopt;
}

void WriteDefaultMask(const SpoilerMessCached &mask) {
	const auto folder = DefaultMaskCacheFolder();
	if (!QDir().mkpath(folder)) {
		return;
	}
	const auto bytes = mask.serialize();
	auto file = QFile(DefaultMaskCachePath(folder));
	if (file.open(QIODevice::WriteOnly) && bytes.size() <= kMaxCacheSize) {
		file.write(bytes);
	}
}

void Register(not_null<SpoilerAnimation*> animation) {
	if (!DefaultAnimationManager) {
		DefaultAnimationManager = new AnimationManager();
		DefaultAnimationManager->animation.init([] {
			for (const auto &animation : DefaultAnimationManager->list) {
				animation->repaint();
			}
		});
		DefaultAnimationManager->animation.start();
	}
	DefaultAnimationManager->list.emplace(animation);
}

void Unregister(not_null<SpoilerAnimation*> animation) {
	Expects(DefaultAnimationManager != nullptr);

	DefaultAnimationManager->list.remove(animation);
	if (DefaultAnimationManager->list.empty()) {
		delete base::take(DefaultAnimationManager);
	}
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

	auto random = base::BufferedRandom<uint32>(count * 5);

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
	const auto paintOneAt = [&](const Particle &particle, crl::time now) {
		if (now <= 0 || now >= singleDuration) {
			return;
		}
		const auto x = (particle.x + int(base::SafeRound(now * particle.dx)))
			% size;
		const auto y = (particle.y + int(base::SafeRound(now * particle.dy)))
			% size;
		const auto opacity = (now < descriptor.particleFadeInDuration)
			? (now / float64(descriptor.particleFadeInDuration))
			: (now > singleDuration - descriptor.particleFadeOutDuration)
			? ((singleDuration - now)
				/ float64(descriptor.particleFadeOutDuration))
			: 1.;
		p.setOpacity(opacity);
		const auto &sprite = sprites[particle.spriteIndex];
		p.drawImage(x, y, sprite);
		if (x + spriteSize > size) {
			p.drawImage(x - size, y, sprite);
			if (y + spriteSize > size) {
				p.drawImage(x, y - size, sprite);
				p.drawImage(x - size, y - size, sprite);
			}
		} else if (y + spriteSize > size) {
			p.drawImage(x, y - size, sprite);
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

void FillSpoilerRect(
		QPainter &p,
		QRect rect,
		const SpoilerMessFrame &frame,
		QPoint originShift) {
	if (rect.isEmpty()) {
		return;
	}
	const auto &image = *frame.image;
	const auto source = frame.source;
	const auto ratio = style::DevicePixelRatio();
	const auto origin = rect.topLeft() + originShift;
	const auto size = source.width() / ratio;
	const auto xSkipFrames = (origin.x() <= rect.x())
		? ((rect.x() - origin.x()) / size)
		: -((origin.x() - rect.x() + size - 1) / size);
	const auto ySkipFrames = (origin.y() <= rect.y())
		? ((rect.y() - origin.y()) / size)
		: -((origin.y() - rect.y() + size - 1) / size);
	const auto xFrom = origin.x() + size * xSkipFrames;
	const auto yFrom = origin.y() + size * ySkipFrames;
	Assert((xFrom <= rect.x())
		&& (yFrom <= rect.y())
		&& (xFrom + size > rect.x())
		&& (yFrom + size > rect.y()));
	const auto xTill = rect.x() + rect.width();
	const auto yTill = rect.y() + rect.height();
	const auto xCount = (xTill - xFrom + size - 1) / size;
	const auto yCount = (yTill - yFrom + size - 1) / size;
	Assert(xCount > 0 && yCount > 0);
	const auto xFullFrom = (xFrom < rect.x()) ? 1 : 0;
	const auto yFullFrom = (yFrom < rect.y()) ? 1 : 0;
	const auto xFullTill = xCount - (xFrom + xCount * size > xTill ? 1 : 0);
	const auto yFullTill = yCount - (yFrom + yCount * size > yTill ? 1 : 0);
	const auto targetRect = [&](int x, int y) {
		return QRect(xFrom + x * size, yFrom + y * size, size, size);
	};
	const auto drawFull = [&](int x, int y) {
		p.drawImage(targetRect(x, y), image, source);
	};
	const auto drawPart = [&](int x, int y) {
		const auto target = targetRect(x, y);
		const auto fill = target.intersected(rect);
		Assert(!fill.isEmpty());
		p.drawImage(fill, image, QRect(
			source.topLeft() + ((fill.topLeft() - target.topLeft()) * ratio),
			fill.size() * ratio));
	};
	if (yFullFrom) {
		for (auto x = 0; x != xCount; ++x) {
			drawPart(x, 0);
		}
	}
	if (yFullFrom < yFullTill) {
		if (xFullFrom) {
			for (auto y = yFullFrom; y != yFullTill; ++y) {
				drawPart(0, y);
			}
		}
		if (xFullFrom < xFullTill) {
			for (auto y = yFullFrom; y != yFullTill; ++y) {
				for (auto x = xFullFrom; x != xFullTill; ++x) {
					drawFull(x, y);
				}
			}
		}
		if (xFullFrom <= xFullTill && xFullTill < xCount) {
			for (auto y = yFullFrom; y != yFullTill; ++y) {
				drawPart(xFullTill, y);
			}
		}
	}
	if (yFullFrom <= yFullTill && yFullTill < yCount) {
		for (auto x = 0; x != xCount; ++x) {
			drawPart(x, yFullTill);
		}
	}
}

void FillSpoilerRect(
		QPainter &p,
		QRect rect,
		ImageRoundRadius radius,
		RectParts corners,
		const SpoilerMessFrame &frame,
		QImage &cornerCache,
		QPoint originShift) {
	if (radius == ImageRoundRadius::None || !corners) {
		FillSpoilerRect(p, rect, frame, originShift);
		return;
	}
	const auto &mask = Images::CornersMask(radius);
	const auto side = mask[0].width() / style::DevicePixelRatio();
	const auto xFillFrom = (corners & RectPart::FullLeft) ? side : 0;
	const auto xFillTo = rect.width()
		- ((corners & RectPart::FullRight) ? side : 0);
	const auto yFillFrom = (corners & RectPart::FullTop) ? side : 0;
	const auto yFillTo = rect.height()
		- ((corners & RectPart::FullBottom) ? side : 0);
	const auto xFill = xFillTo - xFillFrom;
	const auto yFill = yFillTo - yFillFrom;
	if (xFill < 0 || yFill < 0) {
		// Unexpected.. but maybe not fatal, just glitchy.
		FillSpoilerRect(p, rect, frame, originShift);
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto paintPart = [&](int x, int y, int w, int h) {
		FillSpoilerRect(
			p,
			{ rect.x() + x, rect.y() + y, w, h },
			frame,
			originShift - QPoint(x, y));
	};
	const auto paintCorner = [&](QPoint position, const QImage &mask) {
		if (cornerCache.width() < mask.width()
			|| cornerCache.height() < mask.height()) {
			cornerCache = QImage(
				std::max(cornerCache.width(), mask.width()),
				std::max(cornerCache.height(), mask.height()),
				QImage::Format_ARGB32_Premultiplied);
			cornerCache.setDevicePixelRatio(ratio);
		}
		const auto size = mask.size() / ratio;
		const auto target = QRect(QPoint(), size);
		auto q = QPainter(&cornerCache);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		FillSpoilerRect(
			q,
			target,
			frame,
			originShift - rect.topLeft() - position);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.drawImage(target, mask);
		q.end();
		p.drawImage(
			QRect(rect.topLeft() + position, size),
			cornerCache,
			QRect(QPoint(), mask.size()));
	};
	if (yFillFrom > 0) {
		if (xFillFrom > 0) {
			paintCorner({}, mask[0]);
		}
		if (xFill) {
			paintPart(xFillFrom, 0, xFill, yFillFrom);
		}
		if (xFillTo < rect.width()) {
			paintCorner({ xFillTo, 0 }, mask[1]);
		}
	}
	if (yFill) {
		paintPart(0, yFillFrom, rect.width(), yFill);
	}
	if (yFillTo < rect.height()) {
		if (xFillFrom > 0) {
			paintCorner({ 0, yFillTo }, mask[2]);
		}
		if (xFill) {
			paintPart(xFillFrom, yFillTo, xFill, rect.height() - yFillTo);
		}
		if (xFillTo < rect.width()) {
			paintCorner({ xFillTo, yFillTo }, mask[3]);
		}
	}
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

SpoilerMessCached::SpoilerMessCached(
	const SpoilerMessCached &mask,
	const QColor &color)
: SpoilerMessCached(
	style::colorizeImage(*mask.frame(0).image, color),
	mask.framesCount(),
	mask.frameDuration(),
	mask.canvasSize()) {
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

crl::time SpoilerMessCached::frameDuration() const {
	return _frameDuration;
}

int SpoilerMessCached::framesCount() const {
	return _framesCount;
}

int SpoilerMessCached::canvasSize() const {
	return _canvasSize;
}

QByteArray SpoilerMessCached::serialize() const {
	Expects(_frameDuration < std::numeric_limits<int32>::max());

	const auto skip = sizeof(Header);
	auto result = QByteArray(skip, Qt::Uninitialized);
	auto header = Header{
		.version = kVersion,
		.framesCount = _framesCount,
		.canvasSize = _canvasSize,
		.frameDuration = int32(_frameDuration),
	};
	const auto width = int(_image.width());
	const auto height = int(_image.height());
	auto grayscale = QImage(width, height, QImage::Format_Grayscale8);
	{
		auto tobytes = grayscale.bits();
		auto frombytes = _image.constBits();
		const auto toadd = grayscale.bytesPerLine() - width;
		const auto fromadd = _image.bytesPerLine() - (width * 4);
		for (auto y = 0; y != height; ++y) {
			for (auto x = 0; x != width; ++x) {
				*tobytes++ = *frombytes;
				frombytes += 4;
			}
			tobytes += toadd;
			frombytes += fromadd;
		}
	}

	auto device = QBuffer(&result);
	device.open(QIODevice::WriteOnly);
	device.seek(skip);
	grayscale.save(&device, "PNG");
	device.close();
	header.dataLength = result.size() - skip;
	header.dataHash = XXH32(result.data() + skip, header.dataLength, 0);
	memcpy(result.data(), &header, skip);
	return result;
}

std::optional<SpoilerMessCached> SpoilerMessCached::FromSerialized(
		QByteArray data,
		std::optional<Validator> validator) {
	const auto skip = sizeof(Header);
	const auto length = data.size();
	const auto bytes = reinterpret_cast<const uchar*>(data.constData());
	if (length <= skip) {
		return {};
	}
	auto header = Header();
	memcpy(&header, bytes, skip);
	if (header.version != kVersion
		|| header.canvasSize <= 0
		|| header.framesCount <= 0
		|| header.frameDuration <= 0
		|| (validator
			&& (validator->frameDuration != header.frameDuration
				|| validator->framesCount != header.framesCount
				|| validator->canvasSize != header.canvasSize))
		|| (skip + header.dataLength != length)
		|| (XXH32(bytes + skip, header.dataLength, 0) != header.dataHash)) {
		return {};
	}
	auto grayscale = QImage();
	if (!grayscale.loadFromData(bytes + skip, header.dataLength, "PNG")
		|| (grayscale.format() != QImage::Format_Grayscale8)) {
		return {};
	}
	const auto count = header.framesCount;
	const auto rows = (count + kFramesPerRow - 1) / kFramesPerRow;
	const auto columns = std::min(count, kFramesPerRow);
	const auto width = grayscale.width();
	const auto height = grayscale.height();
	if (QSize(width, height) != QSize(columns, rows) * header.canvasSize) {
		return {};
	}
	auto image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	{
		Assert(image.bytesPerLine() % 4 == 0);
		auto toints = reinterpret_cast<uint32*>(image.bits());
		auto frombytes = grayscale.constBits();
		const auto toadd = (image.bytesPerLine() / 4) - width;
		const auto fromadd = grayscale.bytesPerLine() - width;
		for (auto y = 0; y != height; ++y) {
			for (auto x = 0; x != width; ++x) {
				const auto byte = uint32(*frombytes++);
				*toints++ = (byte << 24) | (byte << 16) | (byte << 8) | byte;
			}
			toints += toadd;
			frombytes += fromadd;
		}

	}
	return SpoilerMessCached(
		std::move(image),
		count,
		header.frameDuration,
		header.canvasSize);
}

SpoilerAnimation::SpoilerAnimation(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
	Expects(_repaint != nullptr);
}

SpoilerAnimation::~SpoilerAnimation() {
	if (_animating) {
		_animating = false;
		Unregister(this);
	}
}

int SpoilerAnimation::index(crl::time now, bool paused) {
	const auto add = std::min(now - _last, kDefaultFrameDuration);
	if (anim::Disabled()) {
		paused = true;
	}
	if (!paused || _last) {
		_accumulated += add;
		_last = paused ? 0 : now;
	}
	const auto absolute = (_accumulated / kDefaultFrameDuration);
	if (!paused && !_animating) {
		_animating = true;
		Register(this);
	} else if (paused && _animating) {
		_animating = false;
		Unregister(this);
	}
	return absolute % kDefaultFramesCount;
}

void SpoilerAnimation::repaint() {
	_repaint();
}

void PrepareDefaultSpoilerMess() {
	DefaultMaskSignal = new std::condition_variable();
	DefaultMaskMutex = new std::mutex();
	crl::async([] {
		const auto ratio = style::DevicePixelRatio();
		const auto size = style::ConvertScale(kDefaultCanvasSize) * ratio;
		auto cached = ReadDefaultMask(SpoilerMessCached::Validator{
			.frameDuration = kDefaultFrameDuration,
			.framesCount = kDefaultFramesCount,
			.canvasSize = size,
		});
		if (cached) {
			DefaultMask = new SpoilerMessCached(std::move(*cached));
		} else {
			DefaultMask = new SpoilerMessCached(GenerateSpoilerMess({
				.particleFadeInDuration = kDefaultFadeInDuration,
				.particleShownDuration = kDefaultParticleShownDuration,
				.particleFadeOutDuration = kDefaultFadeOutDuration,
				.particleSizeMin = style::ConvertScaleExact(1.5) * ratio,
				.particleSizeMax = style::ConvertScaleExact(2.) * ratio,
				.particleSpeedMin = style::ConvertScaleExact(10.),
				.particleSpeedMax = style::ConvertScaleExact(20.),
				.particleSpritesCount = 5,
				.particlesCount = kDefaultParticlesCount,
				.canvasSize = size,
				.framesCount = kDefaultFramesCount,
				.frameDuration = kDefaultFrameDuration,
			}));
		}
		auto lock = std::unique_lock(*DefaultMaskMutex);
		DefaultMaskSignal->notify_all();
		if (!cached) {
			WriteDefaultMask(*DefaultMask);
		}
	});
}

const SpoilerMessCached &DefaultSpoilerMask() {
	if (const auto result = DefaultMask.load()) {
		return *result;
	}
	Assert(DefaultMaskSignal != nullptr);
	Assert(DefaultMaskMutex != nullptr);
	while (true) {
		auto lock = std::unique_lock(*DefaultMaskMutex);
		if (const auto result = DefaultMask.load()) {
			return *result;
		}
		DefaultMaskSignal->wait(lock);
	}
}

const SpoilerMessCached &DefaultImageSpoiler() {
	static const auto result = [&] {
		const auto mask = Ui::DefaultSpoilerMask();
		const auto frame = mask.frame(0);
		auto image = QImage(
			frame.image->size(),
			QImage::Format_ARGB32_Premultiplied);
		image.fill(QColor(0, 0, 0, kImageSpoilerDarkenAlpha));
		auto p = QPainter(&image);
		p.drawImage(0, 0, *frame.image);
		p.end();
		return Ui::SpoilerMessCached(
			std::move(image),
			mask.framesCount(),
			mask.frameDuration(),
			mask.canvasSize());
	}();
	return result;
}

} // namespace Ui
