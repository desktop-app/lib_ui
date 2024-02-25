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

constexpr auto kVersion = 2;
constexpr auto kFramesPerRow = 10;
constexpr auto kImageSpoilerDarkenAlpha = 32;
constexpr auto kMaxCacheSize = 5 * 1024 * 1024;
constexpr auto kDefaultFrameDuration = crl::time(33);
constexpr auto kDefaultFramesCount = 60;
constexpr auto kAutoPauseTimeout = crl::time(1000);

[[nodiscard]] SpoilerMessDescriptor DefaultDescriptorText() {
	const auto ratio = style::DevicePixelRatio();
	const auto size = style::ConvertScale(128) * ratio;
	return {
		.particleFadeInDuration = crl::time(200),
		.particleShownDuration = crl::time(200),
		.particleFadeOutDuration = crl::time(200),
		.particleSizeMin = style::ConvertScaleExact(1.5) * ratio,
		.particleSizeMax = style::ConvertScaleExact(2.) * ratio,
		.particleSpeedMin = style::ConvertScaleExact(4.),
		.particleSpeedMax = style::ConvertScaleExact(8.),
		.particleSpritesCount = 5,
		.particlesCount = 9000,
		.canvasSize = size,
		.framesCount = kDefaultFramesCount,
		.frameDuration = kDefaultFrameDuration,
	};
}

[[nodiscard]] SpoilerMessDescriptor DefaultDescriptorImage() {
	const auto ratio = style::DevicePixelRatio();
	const auto size = style::ConvertScale(128) * ratio;
	return {
		.particleFadeInDuration = crl::time(300),
		.particleShownDuration = crl::time(0),
		.particleFadeOutDuration = crl::time(300),
		.particleSizeMin = style::ConvertScaleExact(1.5) * ratio,
		.particleSizeMax = style::ConvertScaleExact(2.) * ratio,
		.particleSpeedMin = style::ConvertScaleExact(10.),
		.particleSpeedMax = style::ConvertScaleExact(20.),
		.particleSpritesCount = 5,
		.particlesCount = 3000,
		.canvasSize = size,
		.framesCount = kDefaultFramesCount,
		.frameDuration = kDefaultFrameDuration,
	};
}

} // namespace

class SpoilerAnimationManager final {
public:
	explicit SpoilerAnimationManager(not_null<SpoilerAnimation*> animation);

	void add(not_null<SpoilerAnimation*> animation);
	void remove(not_null<SpoilerAnimation*> animation);

private:
	void destroyIfEmpty();

	Ui::Animations::Basic _animation;
	base::flat_set<not_null<SpoilerAnimation*>> _list;

};

namespace {

struct DefaultSpoilerWaiter {
	std::condition_variable variable;
	std::mutex mutex;
};
struct DefaultSpoiler {
	std::atomic<const SpoilerMessCached*> cached/* = nullptr*/;
	std::atomic<DefaultSpoilerWaiter*> waiter/* = nullptr*/;
};
DefaultSpoiler DefaultTextMask;
DefaultSpoiler DefaultImageCached;

SpoilerAnimationManager *DefaultAnimationManager/* = nullptr*/;

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
	const auto x = (speedMax > 0)
		? ((RandomIndex(2 * max + 1, random) - max) / float64(max))
		: 0.;
	const auto y = (speedMax > 0)
		? (sqrt(1 - x * x) * (negative ? -1 : 1))
		: 0.;
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
	QPainterPath path;
	path.addRoundedRect(1., 1., width, height, radius, radius);
	p.drawPath(path);
	p.end();
	return result;
}

[[nodiscard]] QString DefaultMaskCacheFolder() {
	const auto base = Integration::Instance().emojiCacheFolder();
	return base.isEmpty() ? QString() : (base + "/spoiler");
}

[[nodiscard]] std::optional<SpoilerMessCached> ReadDefaultMask(
		const QString &name,
		std::optional<SpoilerMessCached::Validator> validator) {
	const auto folder = DefaultMaskCacheFolder();
	if (folder.isEmpty()) {
		return {};
	}
	auto file = QFile(folder + '/' + name);
	return (file.open(QIODevice::ReadOnly) && file.size() <= kMaxCacheSize)
		? SpoilerMessCached::FromSerialized(file.readAll(), validator)
		: std::nullopt;
}

void WriteDefaultMask(
		const QString &name,
		const SpoilerMessCached &mask) {
	const auto folder = DefaultMaskCacheFolder();
	if (!QDir().mkpath(folder)) {
		return;
	}
	const auto bytes = mask.serialize();
	auto file = QFile(folder + '/' + name);
	if (file.open(QIODevice::WriteOnly) && bytes.size() <= kMaxCacheSize) {
		file.write(bytes);
	}
}

void Register(not_null<SpoilerAnimation*> animation) {
	if (DefaultAnimationManager) {
		DefaultAnimationManager->add(animation);
	} else {
		new SpoilerAnimationManager(animation);
	}
}

void Unregister(not_null<SpoilerAnimation*> animation) {
	Expects(DefaultAnimationManager != nullptr);

	DefaultAnimationManager->remove(animation);
}

// DescriptorFactory: (void) -> SpoilerMessDescriptor.
// Postprocess: (unique_ptr<MessCached>) -> unique_ptr<MessCached>.
template <typename DescriptorFactory, typename Postprocess>
void PrepareDefaultSpoiler(
		DefaultSpoiler &spoiler,
		const char *nameFactory,
		DescriptorFactory descriptorFactory,
		Postprocess postprocess) {
	if (spoiler.waiter.load()) {
		return;
	}
	const auto waiter = new DefaultSpoilerWaiter();
	auto expected = (DefaultSpoilerWaiter*)nullptr;
	if (!spoiler.waiter.compare_exchange_strong(expected, waiter)) {
		delete waiter;
		return;
	}
	const auto name = QString::fromUtf8(nameFactory);
	crl::async([=, &spoiler] {
		const auto descriptor = descriptorFactory();
		auto cached = ReadDefaultMask(name, SpoilerMessCached::Validator{
			.frameDuration = descriptor.frameDuration,
			.framesCount = descriptor.framesCount,
			.canvasSize = descriptor.canvasSize,
		});
		spoiler.cached = postprocess(cached
			? std::make_unique<SpoilerMessCached>(std::move(*cached))
			: std::make_unique<SpoilerMessCached>(
				GenerateSpoilerMess(descriptor))
		).release();
		auto lock = std::unique_lock(waiter->mutex);
		waiter->variable.notify_all();
		if (!cached) {
			WriteDefaultMask(name, *spoiler.cached);
		}
	});
}

[[nodiscard]] const SpoilerMessCached &WaitDefaultSpoiler(
		DefaultSpoiler &spoiler) {
	const auto &cached = spoiler.cached;
	if (const auto result = cached.load()) {
		return *result;
	}
	const auto waiter = spoiler.waiter.load();
	Assert(waiter != nullptr);
	while (true) {
		auto lock = std::unique_lock(waiter->mutex);
		if (const auto result = cached.load()) {
			return *result;
		}
		waiter->variable.wait(lock);
	}
}

} // namespace

SpoilerAnimationManager::SpoilerAnimationManager(
	not_null<SpoilerAnimation*> animation)
: _animation([=](crl::time now) {
	for (auto i = begin(_list); i != end(_list);) {
		if ((*i)->repaint(now)) {
			++i;
		} else {
			i = _list.erase(i);
		}
	}
	destroyIfEmpty();
})
, _list{ { animation } } {
	Expects(!DefaultAnimationManager);

	DefaultAnimationManager = this;
	_animation.start();
}

void SpoilerAnimationManager::add(not_null<SpoilerAnimation*> animation) {
	_list.emplace(animation);
}

void SpoilerAnimationManager::remove(not_null<SpoilerAnimation*> animation) {
	_list.remove(animation);
	destroyIfEmpty();
}

void SpoilerAnimationManager::destroyIfEmpty() {
	if (_list.empty()) {
		Assert(DefaultAnimationManager == this);
		delete base::take(DefaultAnimationManager);
	}
}

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
		const auto clamp = [&](int value) {
			return ((value % size) + size) % size;
		};
		const auto x = clamp(
			particle.x + int(base::SafeRound(now * particle.dx)));
		const auto y = clamp(
			particle.y + int(base::SafeRound(now * particle.dy)));
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
		Images::CornersMaskRef mask,
		const SpoilerMessFrame &frame,
		QImage &cornerCache,
		QPoint originShift) {
	using namespace Images;

	if ((!mask.p[kTopLeft] || mask.p[kTopLeft]->isNull())
		&& (!mask.p[kTopRight] || mask.p[kTopRight]->isNull())
		&& (!mask.p[kBottomLeft] || mask.p[kBottomLeft]->isNull())
		&& (!mask.p[kBottomRight] || mask.p[kBottomRight]->isNull())) {
		FillSpoilerRect(p, rect, frame, originShift);
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto cornerSize = [&](int index) {
		const auto corner = mask.p[index];
		return (!corner || corner->isNull()) ? 0 : (corner->width() / ratio);
	};
	const auto verticalSkip = [&](int left, int right) {
		return std::max(cornerSize(left), cornerSize(right));
	};
	const auto fillBg = [&](QRect part) {
		FillSpoilerRect(
			p,
			part.translated(rect.topLeft()),
			frame,
			originShift - rect.topLeft() - part.topLeft());
	};
	const auto fillCorner = [&](int x, int y, int index) {
		const auto position = QPoint(x, y);
		const auto corner = mask.p[index];
		if (!corner || corner->isNull()) {
			return;
		}
		if (cornerCache.width() < corner->width()
			|| cornerCache.height() < corner->height()) {
			cornerCache = QImage(
				std::max(cornerCache.width(), corner->width()),
				std::max(cornerCache.height(), corner->height()),
				QImage::Format_ARGB32_Premultiplied);
			cornerCache.setDevicePixelRatio(ratio);
		}
		const auto size = corner->size() / ratio;
		const auto target = QRect(QPoint(), size);
		auto q = QPainter(&cornerCache);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		FillSpoilerRect(
			q,
			target,
			frame,
			originShift - rect.topLeft() - position);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.drawImage(target, *corner);
		q.end();
		p.drawImage(
			QRect(rect.topLeft() + position, size),
			cornerCache,
			QRect(QPoint(), corner->size()));
	};
	const auto top = verticalSkip(kTopLeft, kTopRight);
	const auto bottom = verticalSkip(kBottomLeft, kBottomRight);
	if (top) {
		const auto left = cornerSize(kTopLeft);
		const auto right = cornerSize(kTopRight);
		if (left) {
			fillCorner(0, 0, kTopLeft);
			if (const auto add = top - left) {
				fillBg({ 0, left, left, add });
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillBg({ left, 0, fill, top });
		}
		if (right) {
			fillCorner(rect.width() - right, 0, kTopRight);
			if (const auto add = top - right) {
				fillBg({ rect.width() - right, right, right, add });
			}
		}
	}
	if (const auto h = rect.height() - top - bottom; h > 0) {
		fillBg({ 0, top, rect.width(), h });
	}
	if (bottom) {
		const auto left = cornerSize(kBottomLeft);
		const auto right = cornerSize(kBottomRight);
		if (left) {
			fillCorner(0, rect.height() - left, kBottomLeft);
			if (const auto add = bottom - left) {
				fillBg({ 0, rect.height() - bottom, left, add });
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillBg({ left, rect.height() - bottom, fill, bottom });
		}
		if (right) {
			fillCorner(
				rect.width() - right,
				rect.height() - right,
				kBottomRight);
			if (const auto add = bottom - right) {
				fillBg({
					rect.width() - right,
					rect.height() - bottom,
					right,
					add,
				});
			}
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
	_scheduled = false;
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

Fn<void()> SpoilerAnimation::repaintCallback() const {
	return _repaint;
}

bool SpoilerAnimation::repaint(crl::time now) {
	if (!_scheduled) {
		_scheduled = true;
		_repaint();
	} else if (_animating && _last && _last + kAutoPauseTimeout <= now) {
		_animating = false;
		return false;
	}
	return true;
}

void PreloadTextSpoilerMask() {
	PrepareDefaultSpoiler(
		DefaultTextMask,
		"text",
		DefaultDescriptorText,
		[](std::unique_ptr<SpoilerMessCached> cached) { return cached; });
}

const SpoilerMessCached &DefaultTextSpoilerMask() {
	[[maybe_unused]] static const auto once = [&] {
		PreloadTextSpoilerMask();
		return 0;
	}();
	return WaitDefaultSpoiler(DefaultTextMask);
}

void PreloadImageSpoiler() {
	const auto postprocess = [](std::unique_ptr<SpoilerMessCached> cached) {
		Expects(cached != nullptr);

		const auto frame = cached->frame(0);
		auto image = QImage(
			frame.image->size(),
			QImage::Format_ARGB32_Premultiplied);
		image.fill(QColor(0, 0, 0, kImageSpoilerDarkenAlpha));
		auto p = QPainter(&image);
		p.drawImage(0, 0, *frame.image);
		p.end();
		return std::make_unique<SpoilerMessCached>(
			std::move(image),
			cached->framesCount(),
			cached->frameDuration(),
			cached->canvasSize());
	};
	PrepareDefaultSpoiler(
		DefaultImageCached,
		"image",
		DefaultDescriptorImage,
		postprocess);
}

const SpoilerMessCached &DefaultImageSpoiler() {
	[[maybe_unused]] static const auto once = [&] {
		PreloadImageSpoiler();
		return 0;
	}();
	return WaitDefaultSpoiler(DefaultImageCached);
}

} // namespace Ui
