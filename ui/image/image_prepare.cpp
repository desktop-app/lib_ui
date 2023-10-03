// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/image/image_prepare.h"

#include "ui/effects/animation_value.h"
#include "ui/style/style_core.h"
#include "ui/painter.h"
#include "base/flat_map.h"
#include "base/debug_log.h"
#include "base/bytes.h"
#include "styles/palette.h"
#include "styles/style_basic.h"

#include <zlib.h>
#include <QtCore/QFile>
#include <QtCore/QBuffer>
#include <QtCore/QMutex>
#include <QtGui/QImageReader>
#include <QtSvg/QSvgRenderer>

#include <jpeglib.h>

namespace Images {
namespace {

// They should be smaller.
constexpr auto kMaxGzipFileSize = 5 * 1024 * 1024;

TG_FORCE_INLINE uint64 BlurGetColors(const uchar *p) {
	return (uint64)p[0]
		+ ((uint64)p[1] << 16)
		+ ((uint64)p[2] << 32)
		+ ((uint64)p[3] << 48);
}

const QImage &EllipseMaskCached(QSize size) {
	const auto key = (uint64(uint32(size.width())) << 32)
		| uint64(uint32(size.height()));

	static auto Masks = base::flat_map<uint64, QImage>();
	static auto Mutex = QMutex();
	auto lock = QMutexLocker(&Mutex);
	const auto i = Masks.find(key);
	if (i != end(Masks)) {
		return i->second;
	}
	lock.unlock();

	auto mask = EllipseMask(size, 1.);

	lock.relock();
	return Masks.emplace(key, std::move(mask)).first->second;
}

std::array<QImage, 4> PrepareCornersMask(int radius) {
	auto result = std::array<QImage, 4>();
	const auto side = radius * style::DevicePixelRatio();
	auto full = QImage(
		QSize(side, side) * 3,
		QImage::Format_ARGB32_Premultiplied);
	full.fill(Qt::transparent);
	{
		QPainter p(&full);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(0, 0, side * 3, side * 3, side, side);
	}
	result[0] = full.copy(0, 0, side, side);
	result[1] = full.copy(side * 2, 0, side, side);
	result[2] = full.copy(0, side * 2, side, side);
	result[3] = full.copy(side * 2, side * 2, side, side);
	for (auto &image : result) {
		image.setDevicePixelRatio(style::DevicePixelRatio());
	}
	return result;
}

template <int kBits> // 4 means 16x16, 3 means 8x8
[[nodiscard]] QImage DitherGeneric(const QImage &image) {
	static_assert(kBits >= 1 && kBits <= 4);

	constexpr auto kSquareSide = (1 << kBits);
	constexpr auto kShift = kSquareSide / 2;
	constexpr auto kMask = (kSquareSide - 1);

	const auto width = image.width();
	const auto height = image.height();
	const auto area = width * height;
	const auto shifts = std::make_unique<uchar[]>(area);
	bytes::set_random(bytes::make_span(shifts.get(), area));

	// shiftx = int(shift & kMask) - kShift;
	// shifty = int((shift >> 4) & kMask) - kShift;
	// Clamp shifts close to edges.
	for (auto y = 0; y != kShift; ++y) {
		const auto min = kShift - y;
		const auto shifted = (min << 4);
		auto shift = shifts.get() + y * width;
		for (const auto till = shift + width; shift != till; ++shift) {
			if (((*shift >> 4) & kMask) < min) {
				*shift = shifted | (*shift & 0x0F);
			}
		}
	}
	for (auto y = height - (kShift - 1); y != height; ++y) {
		const auto max = kShift + (height - y - 1);
		const auto shifted = (max << 4);
		auto shift = shifts.get() + y * width;
		for (const auto till = shift + width; shift != till; ++shift) {
			if (((*shift >> 4) & kMask) > max) {
				*shift = shifted | (*shift & 0x0F);
			}
		}
	}
	for (auto shift = shifts.get(), ytill = shift + area
		; shift != ytill
		; shift += width - kShift) {
		for (const auto till = shift + kShift; shift != till; ++shift) {
			const auto min = (till - shift);
			if ((*shift & kMask) < min) {
				*shift = (*shift & 0xF0) | min;
			}
		}
	}
	for (auto shift = shifts.get(), ytill = shift + area; shift != ytill;) {
		shift += width - (kShift - 1);
		for (const auto till = shift + (kShift - 1); shift != till; ++shift) {
			const auto max = kShift + (till - shift - 1);
			if ((*shift & kMask) > max) {
				*shift = (*shift & 0xF0) | max;
			}
		}
	}

	auto result = image;
	result.detach();

	const auto src = reinterpret_cast<const uint32*>(image.constBits());
	const auto dst = reinterpret_cast<uint32*>(result.bits());
	for (auto index = 0; index != area; ++index) {
		const auto shift = shifts[index];
		const auto shiftx = int(shift & kMask) - kShift;
		const auto shifty = int((shift >> 4) & kMask) - kShift;
		dst[index] = src[index + (shifty * width) + shiftx];
	}

	return result;
}

[[nodiscard]] QImage GenerateSmallComplexGradient(
		const std::vector<QColor> &colors,
		int rotation,
		float progress) {
	const auto positions = std::vector<std::pair<float, float>>{
		{ 0.80f, 0.10f },
		{ 0.60f, 0.20f },
		{ 0.35f, 0.25f },
		{ 0.25f, 0.60f },
		{ 0.20f, 0.90f },
		{ 0.40f, 0.80f },
		{ 0.65f, 0.75f },
		{ 0.75f, 0.40f },
	};
	const auto positionsForPhase = [&](int phase) {
		auto result = std::vector<std::pair<float, float>>(4);
		for (auto i = 0; i != 4; ++i) {
			result[i] = positions[(phase + i * 2) % 8];
			result[i].second = 1.f - result[i].second;
		}
		return result;
	};
	const auto phase = std::clamp(rotation, 0, 315) / 45;
	const auto previousPhase = (phase + 1) % 8;
	const auto previous = positionsForPhase(previousPhase);
	const auto current = positionsForPhase(phase);

	constexpr auto kWidth = 64;
	constexpr auto kHeight = 64;
	static const auto pixelCache = [&] {
		auto result = std::make_unique<float[]>(kWidth * kHeight * 2);
		const auto invwidth = 1.f / kWidth;
		const auto invheight = 1.f / kHeight;
		auto floats = result.get();
		for (auto y = 0; y != kHeight; ++y) {
			const auto directPixelY = y * invheight;
			const auto centerDistanceY = directPixelY - 0.5f;
			const auto centerDistanceY2 = centerDistanceY * centerDistanceY;
			for (auto x = 0; x != kWidth; ++x) {
				const auto directPixelX = x * invwidth;
				const auto centerDistanceX = directPixelX - 0.5f;
				const auto centerDistance = sqrtf(
					centerDistanceX * centerDistanceX + centerDistanceY2);

				const auto swirlFactor = 0.35f * centerDistance;
				const auto theta = swirlFactor * swirlFactor * 0.8f * 8.0f;
				const auto sinTheta = sinf(theta);
				const auto cosTheta = cosf(theta);
				*floats++ = std::max(
					0.0f,
					std::min(
						1.0f,
						(0.5f
							+ centerDistanceX * cosTheta
							- centerDistanceY * sinTheta)));
				*floats++ = std::max(
					0.0f,
					std::min(
						1.0f,
						(0.5f
							+ centerDistanceX * sinTheta
							+ centerDistanceY * cosTheta)));
			}
		}
		return result;
	}();
	const auto colorsCount = int(colors.size());
	auto colorsFloat = std::vector<std::array<float, 3>>(colorsCount);
	for (auto i = 0; i != colorsCount; ++i) {
		colorsFloat[i] = {
			float(colors[i].red()),
			float(colors[i].green()),
			float(colors[i].blue()),
		};
	}
	auto result = QImage(
		kWidth,
		kHeight,
		QImage::Format_RGB32);
	Assert(result.bytesPerLine() == kWidth * 4);

	auto cache = pixelCache.get();
	auto pixels = reinterpret_cast<uint32*>(result.bits());
	for (auto y = 0; y != kHeight; ++y) {
		for (auto x = 0; x != kWidth; ++x) {
			const auto pixelX = *cache++;
			const auto pixelY = *cache++;

			auto distanceSum = 0.f;
			auto r = 0.f;
			auto g = 0.f;
			auto b = 0.f;
			for (auto i = 0; i != colorsCount; ++i) {
				const auto colorX = previous[i].first
					+ (current[i].first - previous[i].first) * progress;
				const auto colorY = previous[i].second
					+ (current[i].second - previous[i].second) * progress;

				const auto dx = pixelX - colorX;
				const auto dy = pixelY - colorY;
				const auto distance = std::max(
					0.0f,
					0.9f - sqrtf(dx * dx + dy * dy));
				const auto square = distance * distance;
				const auto fourth = square * square;
				distanceSum += fourth;

				r += fourth * colorsFloat[i][0];
				g += fourth * colorsFloat[i][1];
				b += fourth * colorsFloat[i][2];
			}

			const auto red = uint32(r / distanceSum);
			const auto green = uint32(g / distanceSum);
			const auto blue = uint32(b / distanceSum);
			*pixels++ = 0xFF000000U | (red << 16) | (green << 8) | blue;
		}
	}
	return result;
}

[[nodiscard]] QImage GenerateComplexGradient(
		QSize size,
		const std::vector<QColor> &colors,
		int rotation,
		float progress) {
	auto exact = GenerateSmallComplexGradient(colors, rotation, progress);
	return (exact.size() == size)
		? exact
		: exact.scaled(
			size,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
}

} // namespace

QPixmap PixmapFast(QImage &&image) {
	Expects(image.format() == QImage::Format_ARGB32_Premultiplied
		|| image.format() == QImage::Format_RGB32);

	return QPixmap::fromImage(std::move(image), Qt::NoFormatConversion);
}

const std::array<QImage, 4> &CornersMask(ImageRoundRadius radius) {
	if (radius == ImageRoundRadius::Large) {
		static auto Mask = PrepareCornersMask(st::roundRadiusLarge);
		return Mask;
	} else {
		static auto Mask = PrepareCornersMask(st::roundRadiusSmall);
		return Mask;
	}
}

std::array<QImage, 4> PrepareCorners(
		ImageRoundRadius radius,
		const style::color &color) {
	auto result = CornersMask(radius);
	for (auto &image : result) {
		style::colorizeImage(image, color->c, &image);
	}
	return result;
}

std::array<QImage, 4> CornersMask(int radius) {
	return PrepareCornersMask(radius);
}

QImage EllipseMask(QSize size, double ratio) {
	size *= ratio;
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	QPainter p(&result);
	PainterHighQualityEnabler hq(p);
	p.setBrush(Qt::white);
	p.setPen(Qt::NoPen);
	p.drawEllipse(QRect(QPoint(), size));
	p.end();

	result.setDevicePixelRatio(ratio);
	return result;
}

std::array<QImage, 4> PrepareCorners(
		int radius,
		const style::color &color) {
	auto result = CornersMask(radius);
	for (auto &image : result) {
		style::colorizeImage(image, color->c, &image);
	}
	return result;
}

[[nodiscard]] QByteArray UnpackGzip(const QByteArray &bytes) {
	z_stream stream;
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	stream.avail_in = 0;
	stream.next_in = nullptr;
	int res = inflateInit2(&stream, 16 + MAX_WBITS);
	if (res != Z_OK) {
		return bytes;
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = QByteArray(kMaxGzipFileSize + 1, char(0));
	stream.avail_in = bytes.size();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
	stream.avail_out = 0;
	while (!stream.avail_out) {
		stream.avail_out = result.size();
		stream.next_out = reinterpret_cast<Bytef*>(result.data());
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			return bytes;
		} else if (!stream.avail_out) {
			return bytes;
		}
	}
	result.resize(result.size() - stream.avail_out);
	return result;
}

[[nodiscard]] ReadResult ReadGzipSvg(const ReadArgs &args) {
	const auto bytes = UnpackGzip(args.content);
	if (bytes.isEmpty()) {
		LOG(("Svg Error: Couldn't unpack gzip-ed content."));
		return {};
	}
	auto renderer = QSvgRenderer(bytes);
	if (!renderer.isValid()) {
		LOG(("Svg Error: Invalid data."));
		return {};
	}
	auto size = renderer.defaultSize();
	if (!args.maxSize.isEmpty()
		&& (size.width() > args.maxSize.width()
			|| size.height() > args.maxSize.height())) {
		size = size.scaled(args.maxSize, Qt::KeepAspectRatio);
	}
	if (size.isEmpty()) {
		LOG(("Svg Error: Bad size %1x%2."
			).arg(renderer.defaultSize().width()
			).arg(renderer.defaultSize().height()));
		return {};
	}
	auto result = ReadResult();
	result.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	result.image.fill(Qt::transparent);
	{
		QPainter p(&result.image);
		renderer.render(&p, QRect(QPoint(), size));
	}
	result.format = "svg";
	return result;
}

[[nodiscard]] ReadResult ReadOther(const ReadArgs &args) {
	auto bytes = args.content;
	if (bytes.isEmpty()) {
		return {};
	}
	auto buffer = QBuffer(&bytes);
	auto reader = QImageReader(&buffer);
	reader.setAutoTransform(true);
	if (!reader.canRead()) {
		return {};
	}
	const auto size = reader.size();
	if (size.width() * size.height() > kReadMaxArea) {
		return {};
	}
	auto result = ReadResult();
	result.format = reader.format().toLower();
	result.animated = reader.supportsAnimation()
		&& (reader.imageCount() > 1);
	if (!reader.read(&result.image) || result.image.isNull()) {
		return {};
	}
	return result;
}

ReadResult Read(ReadArgs &&args) {
	if (args.content.isEmpty()) {
		if (args.path.isEmpty()) {
			return {};
		}
		auto file = QFile(args.path);
		if (file.size() > kReadBytesLimit
			|| !file.open(QIODevice::ReadOnly)) {
			return {};
		}
		args.content = file.readAll();
	}
	auto result = args.gzipSvg ? ReadGzipSvg(args) : ReadOther(args);
	if (result.image.isNull()) {
		args = ReadArgs();
		return {};
	}
	if (args.returnContent) {
		result.content = args.content;
	} else {
		args.content = QByteArray();
	}
	if (!args.maxSize.isEmpty()
		&& (result.image.width() > args.maxSize.width()
			|| result.image.height() > args.maxSize.height())) {
		result.image = result.image.scaled(
			args.maxSize,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	if (args.forceOpaque && result.format != qstr("jpeg")) {
		result.image = Opaque(std::move(result.image));
	}
	return result;
}

[[nodiscard]] Options RoundOptions(
		ImageRoundRadius radius,
		RectParts corners) {
	const auto withCorners = [&](Option rounding) {
		if (rounding == Option::None) {
			return Options();
		}
		const auto corner = [&](RectPart part, Option skip) {
			return !(corners & part) ? skip : Option();
		};
		return rounding
			| corner(RectPart::TopLeft, Option::RoundSkipTopLeft)
			| corner(RectPart::TopRight, Option::RoundSkipTopRight)
			| corner(RectPart::BottomLeft, Option::RoundSkipBottomLeft)
			| corner(RectPart::BottomRight, Option::RoundSkipBottomRight);
	};
	return withCorners((radius == ImageRoundRadius::Large)
		? Option::RoundLarge
		: (radius == ImageRoundRadius::Small)
		? Option::RoundSmall
		: (radius == ImageRoundRadius::Ellipse)
		? Option::RoundCircle
		: Option::None);
}

QImage Blur(QImage &&image, bool ignoreAlpha) {
	if (image.isNull()) {
		return std::move(image);
	}
	const auto ratio = image.devicePixelRatio();
	const auto format = image.format();
	if (format != QImage::Format_RGB32
		&& format != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
	}

	auto pix = image.bits();
	if (!pix) {
		return std::move(image);
	}
	const auto w = image.width();
	const auto h = image.height();
	const auto radius = 3;
	const auto r1 = radius + 1;
	const auto div = radius * 2 + 1;
	const auto stride = w * 4;
	if (radius >= 16 || div >= w || div >= h || stride > w * 4) {
		return std::move(image);
	}
	const auto withalpha = !ignoreAlpha && image.hasAlphaChannel();
	if (withalpha) {
		auto smaller = QImage(image.size(), image.format());
		{
			QPainter p(&smaller);
			PainterHighQualityEnabler hq(p);

			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, w, h, Qt::transparent);
			p.drawImage(
				QRect(radius, radius, w - 2 * radius, h - 2 * radius),
				image,
				QRect(0, 0, w, h));
		}
		smaller.setDevicePixelRatio(ratio);
		auto was = std::exchange(image, base::take(smaller));
		Assert(!image.isNull());

		pix = image.bits();
		if (!pix) return was;
	}
	const auto buffer = std::make_unique<uint64[]>(w * h);
	const auto rgb = buffer.get();

	int x, y, i;

	int yw = 0;
	const int we = w - r1;
	for (y = 0; y < h; y++) {
		uint64 cur = BlurGetColors(&pix[yw]);
		uint64 rgballsum = -radius * cur;
		uint64 rgbsum = cur * ((r1 * (r1 + 1)) >> 1);

		for (i = 1; i <= radius; i++) {
			uint64 cur = BlurGetColors(&pix[yw + i * 4]);
			rgbsum += cur * (r1 - i);
			rgballsum += cur;
		}

		x = 0;

#define update(start, middle, end) \
rgb[y * w + x] = (rgbsum >> 4) & 0x00FF00FF00FF00FFLL; \
rgballsum += BlurGetColors(&pix[yw + (start) * 4]) - 2 * BlurGetColors(&pix[yw + (middle) * 4]) + BlurGetColors(&pix[yw + (end) * 4]); \
rgbsum += rgballsum; \
x++;

		while (x < r1) {
			update(0, x, x + r1);
		}
		while (x < we) {
			update(x - r1, x, x + r1);
		}
		while (x < w) {
			update(x - r1, x, w - 1);
		}

#undef update

		yw += stride;
	}

	const int he = h - r1;
	for (x = 0; x < w; x++) {
		uint64 rgballsum = -radius * rgb[x];
		uint64 rgbsum = rgb[x] * ((r1 * (r1 + 1)) >> 1);
		for (i = 1; i <= radius; i++) {
			rgbsum += rgb[i * w + x] * (r1 - i);
			rgballsum += rgb[i * w + x];
		}

		y = 0;
		int yi = x * 4;

#define update(start, middle, end) \
uint64 res = rgbsum >> 4; \
pix[yi] = res & 0xFF; \
pix[yi + 1] = (res >> 16) & 0xFF; \
pix[yi + 2] = (res >> 32) & 0xFF; \
pix[yi + 3] = (res >> 48) & 0xFF; \
rgballsum += rgb[x + (start) * w] - 2 * rgb[x + (middle) * w] + rgb[x + (end) * w]; \
rgbsum += rgballsum; \
y++; \
yi += stride;

		while (y < r1) {
			update(0, y, y + r1);
		}
		while (y < he) {
			update(y - r1, y, y + r1);
		}
		while (y < h) {
			update(y - r1, y, h - 1);
		}

#undef update
	}

	return std::move(image);
}

[[nodiscard]] QImage BlurLargeImage(QImage &&image, int radius) {
	const auto width = image.width();
	const auto height = image.height();
	if (width <= radius || height <= radius || radius < 1) {
		return std::move(image);
	}

	if (image.format() != QImage::Format_RGB32
		&& image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	const auto pixels = image.bits();

	const auto width_m1 = width - 1;
	const auto height_m1 = height - 1;
	const auto widthxheight = width * height;
	const auto div = 2 * radius + 1;
	const auto radius_p1 = radius + 1;
	const auto divsum = radius_p1 * radius_p1;

	const auto dvcount = 256 * divsum;
	const auto buffers = (div * 3) // stack
		+ std::max(width, height) // vmin
		+ widthxheight * 3 // rgb
		+ dvcount; // dv
	auto storage = std::vector<int>(buffers);
	auto taken = 0;
	const auto take = [&](int size) {
		const auto result = gsl::make_span(storage).subspan(taken, size);
		taken += size;
		return result;
	};

	// Small buffers
	const auto stack = take(div * 3).data();
	const auto vmin = take(std::max(width, height)).data();

	// Large buffers
	const auto rgb = take(widthxheight * 3).data();
	const auto dvs = take(dvcount);

	auto &&ints = ranges::views::ints;
	for (auto &&[value, index] : ranges::views::zip(dvs, ints(0, ranges::unreachable))) {
		value = (index / divsum);
	}
	const auto dv = dvs.data();

	// Variables
	auto stackpointer = 0;
	for (const auto x : ints(0, width)) {
		vmin[x] = std::min(x + radius_p1, width_m1);
	}
	for (const auto y : ints(0, height)) {
		auto rinsum = 0;
		auto ginsum = 0;
		auto binsum = 0;
		auto routsum = 0;
		auto goutsum = 0;
		auto boutsum = 0;
		auto rsum = 0;
		auto gsum = 0;
		auto bsum = 0;

		const auto y_width = y * width;
		for (const auto i : ints(-radius, radius + 1)) {
			const auto sir = &stack[(i + radius) * 3];
			const auto x = std::clamp(i, 0, width_m1);
			const auto offset = (y_width + x) * 4;
			sir[0] = pixels[offset];
			sir[1] = pixels[offset + 1];
			sir[2] = pixels[offset + 2];

			const auto rbs = radius_p1 - std::abs(i);
			rsum += sir[0] * rbs;
			gsum += sir[1] * rbs;
			bsum += sir[2] * rbs;

			if (i > 0) {
				rinsum += sir[0];
				ginsum += sir[1];
				binsum += sir[2];
			} else {
				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];
			}
		}
		stackpointer = radius;

		for (const auto x : ints(0, width)) {
			const auto position = (y_width + x) * 3;
			rgb[position] = dv[rsum];
			rgb[position + 1] = dv[gsum];
			rgb[position + 2] = dv[bsum];

			rsum -= routsum;
			gsum -= goutsum;
			bsum -= boutsum;

			const auto stackstart = (stackpointer - radius + div) % div;
			const auto sir = &stack[stackstart * 3];

			routsum -= sir[0];
			goutsum -= sir[1];
			boutsum -= sir[2];

			const auto offset = (y_width + vmin[x]) * 4;
			sir[0] = pixels[offset];
			sir[1] = pixels[offset + 1];
			sir[2] = pixels[offset + 2];
			rinsum += sir[0];
			ginsum += sir[1];
			binsum += sir[2];

			rsum += rinsum;
			gsum += ginsum;
			bsum += binsum;
			{
				stackpointer = (stackpointer + 1) % div;
				const auto sir = &stack[stackpointer * 3];

				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];

				rinsum -= sir[0];
				ginsum -= sir[1];
				binsum -= sir[2];
			}
		}
	}

	for (const auto y : ints(0, height)) {
		vmin[y] = std::min(y + radius_p1, height_m1) * width;
	}
	for (const auto x : ints(0, width)) {
		auto rinsum = 0;
		auto ginsum = 0;
		auto binsum = 0;
		auto routsum = 0;
		auto goutsum = 0;
		auto boutsum = 0;
		auto rsum = 0;
		auto gsum = 0;
		auto bsum = 0;
		for (const auto i : ints(-radius, radius + 1)) {
			const auto y = std::clamp(i, 0, height_m1);
			const auto position = (y * width + x) * 3;
			const auto sir = &stack[(i + radius) * 3];

			sir[0] = rgb[position];
			sir[1] = rgb[position + 1];
			sir[2] = rgb[position + 2];

			const auto rbs = radius_p1 - std::abs(i);
			rsum += sir[0] * rbs;
			gsum += sir[1] * rbs;
			bsum += sir[2] * rbs;
			if (i > 0) {
				rinsum += sir[0];
				ginsum += sir[1];
				binsum += sir[2];
			} else {
				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];
			}
		}
		stackpointer = radius;
		for (const auto y : ints(0, height)) {
			const auto offset = (y * width + x) * 4;
			pixels[offset] = dv[rsum];
			pixels[offset + 1] = dv[gsum];
			pixels[offset + 2] = dv[bsum];
			rsum -= routsum;
			gsum -= goutsum;
			bsum -= boutsum;

			const auto stackstart = (stackpointer - radius + div) % div;
			const auto sir = &stack[stackstart * 3];

			routsum -= sir[0];
			goutsum -= sir[1];
			boutsum -= sir[2];

			const auto position = (vmin[y] + x) * 3;
			sir[0] = rgb[position];
			sir[1] = rgb[position + 1];
			sir[2] = rgb[position + 2];

			rinsum += sir[0];
			ginsum += sir[1];
			binsum += sir[2];

			rsum += rinsum;
			gsum += ginsum;
			bsum += binsum;
			{
				stackpointer = (stackpointer + 1) % div;
				const auto sir = &stack[stackpointer * 3];

				routsum += sir[0];
				goutsum += sir[1];
				boutsum += sir[2];

				rinsum -= sir[0];
				ginsum -= sir[1];
				binsum -= sir[2];
			}
		}
	}
	return std::move(image);
}

[[nodiscard]] QImage DitherImage(const QImage &image) {
	Expects(image.bytesPerLine() == image.width() * 4);

	const auto width = image.width();
	const auto height = image.height();
	const auto min = std::min(width, height);
	const auto max = std::max(width, height);
	if (max >= 1024 && min >= 512) {
		return DitherGeneric<4>(image);
	} else if (max >= 512 && min >= 256) {
		return DitherGeneric<3>(image);
	} else if (max >= 256 && min >= 128) {
		return DitherGeneric<2>(image);
	} else if (min >= 32) {
		return DitherGeneric<1>(image);
	}
	return image;
}

[[nodiscard]] QImage GenerateGradient(
		QSize size,
		const std::vector<QColor> &colors,
		int rotation,
		float progress) {
	Expects(!colors.empty());
	Expects(colors.size() <= 4);

	if (size.isEmpty()) {
		return QImage();
	} else if (colors.size() > 2) {
		return GenerateComplexGradient(size, colors, rotation, progress);
	} else {
		return GenerateLinearGradient(size, colors, rotation);
	}
}

QImage GenerateLinearGradient(
		QSize size,
		const std::vector<QColor> &colors,
		int rotation) {
	Expects(!colors.empty());

	auto result = QImage(size, QImage::Format_RGB32);
	if (colors.size() == 1) {
		result.fill(colors.front());
		return result;
	}

	auto p = QPainter(&result);
	const auto width = size.width();
	const auto height = size.height();
	const auto [start, finalStop] = [&]() -> std::pair<QPoint, QPoint> {
		const auto type = std::clamp(rotation, 0, 315) / 45;
		switch (type) {
		case 0: return { { 0, 0 }, { 0, height } };
		case 1: return { { width, 0 }, { 0, height } };
		case 2: return { { width, 0 }, { 0, 0 } };
		case 3: return { { width, height }, { 0, 0 } };
		case 4: return { { 0, height }, { 0, 0 } };
		case 5: return { { 0, height }, { width, 0 } };
		case 6: return { { 0, 0 }, { width, 0 } };
		case 7: return { { 0, 0 }, { width, height } };
		}
		Unexpected("Rotation value in GenerateDitheredGradient.");
	}();
	auto gradient = QLinearGradient(start, finalStop);

	if (colors.size() == 2) {
		gradient.setStops(QGradientStops{
			{ 0.0, colors[0] },
			{ 1.0, colors[1] }
		});
	} else {
		auto stops = QGradientStops();
		const auto step = 1. / (colors.size() - 1);
		auto point = 0.;
		for (const auto &color : colors) {
			stops.append({ point, color });
			point += step;
		}
		gradient.setStops(std::move(stops));
	}
	p.fillRect(QRect(QPoint(), size), QBrush(std::move(gradient)));
	p.end();

	return result;
}

QImage GenerateShadow(
		int height,
		int topAlpha,
		int bottomAlpha,
		QColor color) {
	Expects(topAlpha >= 0 && topAlpha < 256);
	Expects(bottomAlpha >= 0 && bottomAlpha < 256);
	Expects(height * style::DevicePixelRatio() < 65536);

	const auto base = (uint32(color.red()) << 16)
		| (uint32(color.green()) << 8)
		| uint32(color.blue());
	const auto premultiplied = (topAlpha == bottomAlpha) || !base;
	auto result = QImage(
		QSize(1, height * style::DevicePixelRatio()),
		(premultiplied
			? QImage::Format_ARGB32_Premultiplied
			: QImage::Format_ARGB32));
	if (topAlpha == bottomAlpha) {
		color.setAlpha(topAlpha);
		result.fill(color);
		return result;
	}
	constexpr auto kShift = 16;
	constexpr auto kMultiply = (1U << kShift);
	const auto values = std::abs(topAlpha - bottomAlpha);
	const auto rows = uint32(result.height());
	const auto step = (values * kMultiply) / (rows - 1);
	const auto till = rows * uint32(step);
	Assert(result.bytesPerLine() == sizeof(uint32));
	auto ints = reinterpret_cast<uint32*>(result.bits());
	if (topAlpha < bottomAlpha) {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = base | ((topAlpha + (i >> kShift)) << 24);
		}
	} else {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = base | ((topAlpha - (i >> kShift)) << 24);
		}
	}
	if (!premultiplied) {
		result = std::move(result).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	return result;
}

QImage Circle(QImage &&image, QRect target) {
	Expects(!image.isNull());

	if (target.isNull()) {
		target = QRect(QPoint( ), image.size());
	} else {
		Assert(QRect(QPoint(), image.size()).contains(target));
	}

	image = std::move(image).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	Assert(!image.isNull());

	const auto ratio = image.devicePixelRatio();
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(
		QRectF(target.topLeft() / ratio, target.size() / ratio),
		EllipseMaskCached(target.size()));
	p.end();

	return std::move(image);
}

QImage Round(
		QImage &&image,
		CornersMaskRef mask,
		QRect target) {
	if (target.isNull()) {
		target = QRect(QPoint(), image.size());
	} else {
		Assert(QRect(QPoint(), image.size()).contains(target));
	}
	const auto targetWidth = target.width();
	const auto targetHeight = target.height();

	image = std::move(image).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	Assert(!image.isNull());

	// We need to detach image first (if it is shared), before we
	// count some offsets using QImage::bytesPerLine etc, because
	// bytesPerLine may change on detach, this leads to crashes:
	// Real image bytesPerLine is smaller than the one we use for offsets.
	auto ints = reinterpret_cast<uint32*>(image.bits());

	constexpr auto kImageIntsPerPixel = 1;
	const auto imageIntsPerLine = (image.bytesPerLine() >> 2);
	Assert(image.depth() == ((kImageIntsPerPixel * sizeof(uint32)) << 3));
	Assert(image.bytesPerLine() == (imageIntsPerLine << 2));
	const auto maskCorner = [&](
			const QImage *mask,
			bool right = false,
			bool bottom = false) {
		const auto maskWidth = mask ? mask->width() : 0;
		const auto maskHeight = mask ? mask->height() : 0;
		if (!maskWidth
			|| !maskHeight
			|| targetWidth < maskWidth
			|| targetHeight < maskHeight) {
			return;
		}

		const auto maskBytesPerPixel = (mask->depth() >> 3);
		const auto maskBytesPerLine = mask->bytesPerLine();
		const auto maskBytesAdded = maskBytesPerLine
			- maskWidth * maskBytesPerPixel;
		Assert(maskBytesAdded >= 0);
		Assert(mask->depth() == (maskBytesPerPixel << 3));
		const auto imageIntsAdded = imageIntsPerLine
			- maskWidth * kImageIntsPerPixel;
		Assert(imageIntsAdded >= 0);
		auto imageInts = ints + target.x() + target.y() * imageIntsPerLine;
		if (right) {
			imageInts += targetWidth - maskWidth;
		}
		if (bottom) {
			imageInts += (targetHeight - maskHeight) * imageIntsPerLine;
		}
		auto maskBytes = mask->constBits();
		for (auto y = 0; y != maskHeight; ++y) {
			for (auto x = 0; x != maskWidth; ++x) {
				auto opacity = static_cast<anim::ShiftedMultiplier>(*maskBytes) + 1;
				*imageInts = anim::unshifted(anim::shifted(*imageInts) * opacity);
				maskBytes += maskBytesPerPixel;
				imageInts += kImageIntsPerPixel;
			}
			maskBytes += maskBytesAdded;
			imageInts += imageIntsAdded;
		}
	};

	maskCorner(mask.p[0]);
	maskCorner(mask.p[1], true);
	maskCorner(mask.p[2], false, true);
	maskCorner(mask.p[3], true, true);

	return std::move(image);
}

QImage Round(
		QImage &&image,
		gsl::span<const QImage, 4> cornerMasks,
		RectParts corners,
		QRect target) {
	return Round(std::move(image), CornersMaskRef({
		(corners & RectPart::TopLeft) ? &cornerMasks[0] : nullptr,
		(corners & RectPart::TopRight) ? &cornerMasks[1] : nullptr,
		(corners & RectPart::BottomLeft) ? &cornerMasks[2] : nullptr,
		(corners & RectPart::BottomRight) ? &cornerMasks[3] : nullptr,
	}), target);
}

QImage Round(
		QImage &&image,
		ImageRoundRadius radius,
		RectParts corners,
		QRect target) {
	if (!static_cast<int>(corners)) {
		return std::move(image);
	} else if (radius == ImageRoundRadius::Ellipse) {
		Assert((corners & RectPart::AllCorners) == RectPart::AllCorners);
		return Circle(std::move(image), target);
	}
	Assert(!image.isNull());

	const auto masks = CornersMask(radius);
	return Round(std::move(image), masks, corners, target);
}

QImage Round(QImage &&image, Options options, QRect target) {
	if (options & Option::RoundCircle) {
		return Circle(std::move(image), target);
	} else if (!(options & (Option::RoundLarge | Option::RoundSmall))) {
		return std::move(image);
	}
	const auto corner = [&](Option skip, RectPart part) {
		return !(options & skip) ? part : RectPart::None;
	};
	return Round(
		std::move(image),
		((options & Option::RoundLarge)
			? ImageRoundRadius::Large
			: ImageRoundRadius::Small),
		(corner(Option::RoundSkipTopLeft, RectPart::TopLeft)
			| corner(Option::RoundSkipTopRight, RectPart::TopRight)
			| corner(Option::RoundSkipBottomLeft, RectPart::BottomLeft)
			| corner(Option::RoundSkipBottomRight, RectPart::BottomRight)),
		target);
}

QImage Colored(QImage &&image, style::color add) {
	return Colored(std::move(image), add->c);
}

QImage Colored(QImage &&image, QColor add) {
	const auto format = image.format();
	if (format != QImage::Format_RGB32
		&& format != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}

	if (const auto pix = image.bits()) {
		const auto ca = add.alpha();
		const auto cr = add.red() * (ca + 1);
		const auto cg = add.green() * (ca + 1);
		const auto cb = add.blue() * (ca + 1);
		const auto ra = (0x100 - ca) * 0x100;
		const auto w = image.width();
		const auto h = image.height();
		const auto add = image.bytesPerLine() - (w * 4);
		auto i = index_type();
		for (auto y = 0; y != h; ++y) {
			for (auto to = i + (w * 4); i != to; i += 4) {
				const auto a = pix[i + 3] + 1;
				pix[i + 0] = (ra * pix[i + 0] + a * cb) >> 16;
				pix[i + 1] = (ra * pix[i + 1] + a * cg) >> 16;
				pix[i + 2] = (ra * pix[i + 2] + a * cr) >> 16;
			}
			i += add;
		}
	}
	return std::move(image);
}

QImage Opaque(QImage &&image) {
	if (image.hasAlphaChannel()) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
		auto ints = reinterpret_cast<uint32*>(image.bits());
		const auto bg = anim::shifted(QColor(Qt::white));
		const auto width = image.width();
		const auto height = image.height();
		const auto addPerLine = (image.bytesPerLine() / sizeof(uint32)) - width;
		for (auto y = 0; y != height; ++y) {
			for (auto x = 0; x != width; ++x) {
				const auto components = anim::shifted(*ints);
				*ints++ = anim::unshifted(components * 256
					+ bg * (256 - anim::getAlpha(components)));
			}
			ints += addPerLine;
		}
	}
	return std::move(image);
}

QImage Prepare(QImage image, int w, int h, const PrepareArgs &args) {
	Expects(!image.isNull());

	if (args.options & Option::Blur) {
		image = Blur(std::move(image));
		Assert(!image.isNull());
	}
	if (w <= 0
		|| (w == image.width() && (h <= 0 || h == image.height()))) {
	} else if (h <= 0) {
		image = image.scaledToWidth(
			w,
			((args.options & Images::Option::FastTransform)
				? Qt::FastTransformation
				: Qt::SmoothTransformation));
		Assert(!image.isNull());
	} else {
		image = image.scaled(
			w,
			h,
			Qt::IgnoreAspectRatio,
			((args.options & Images::Option::FastTransform)
				? Qt::FastTransformation
				: Qt::SmoothTransformation));
		Assert(!image.isNull());
	}
	auto outer = args.outer;
	if (!outer.isEmpty()) {
		const auto ratio = style::DevicePixelRatio();
		outer *= ratio;
		if (outer != QSize(w, h)) {
			image.setDevicePixelRatio(ratio);
			auto result = QImage(outer, QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(ratio);
			if (args.options & Images::Option::TransparentBackground) {
				result.fill(Qt::transparent);
			}
			{
				QPainter p(&result);
				if (!(args.options & Images::Option::TransparentBackground)) {
					if (w < outer.width() || h < outer.height()) {
						p.fillRect(
							QRect({}, result.size() / ratio),
							Qt::black);
					}
				}
				p.drawImage(
					(result.width() - image.width()) / (2 * ratio),
					(result.height() - image.height()) / (2 * ratio),
					image);
			}
			image = std::move(result);
			Assert(!image.isNull());
		}
	}

	if (args.options
		& (Option::RoundCircle | Option::RoundLarge | Option::RoundSmall)) {
		image = Round(std::move(image), args.options);
		Assert(!image.isNull());
	}
	if (args.colored) {
		image = Colored(std::move(image), *args.colored);
	}
	image.setDevicePixelRatio(style::DevicePixelRatio());
	return image;
}

bool IsProgressiveJpeg(const QByteArray &bytes) {
	try {
		struct jpeg_decompress_struct info;
		struct jpeg_error_mgr jerr;

		info.err = jpeg_std_error(&jerr);
		jerr.error_exit = [](j_common_ptr cinfo) {
			(*cinfo->err->output_message)(cinfo);
			throw std::exception();
		};

		jpeg_create_decompress(&info);
		const auto guard = gsl::finally([&] {
			jpeg_destroy_decompress(&info);
		});

		jpeg_mem_src(
			&info,
			reinterpret_cast<const unsigned char*>(bytes.data()),
			bytes.size());
		if (jpeg_read_header(&info, TRUE) != 1) {
			return false;
		}

		return (info.progressive_mode > 0);
	} catch (...) {
		return false;
	}
}

QByteArray MakeProgressiveJpeg(const QByteArray &bytes) {
	try {
		struct jpeg_decompress_struct srcinfo;
		struct jpeg_compress_struct dstinfo;
		struct jpeg_error_mgr jerr;

		srcinfo.err = jpeg_std_error(&jerr);
		dstinfo.err = jpeg_std_error(&jerr);
		jerr.error_exit = [](j_common_ptr cinfo) {
			(*cinfo->err->output_message)(cinfo);
			throw std::exception();
		};

		jpeg_create_decompress(&srcinfo);
		const auto srcguard = gsl::finally([&] {
			jpeg_abort_decompress(&srcinfo);
			jpeg_destroy_decompress(&srcinfo);
		});

		jpeg_create_compress(&dstinfo);
		const auto dstguard = gsl::finally([&] {
			jpeg_abort_compress(&dstinfo);
			jpeg_destroy_compress(&dstinfo);
		});

		jpeg_mem_src(
			&srcinfo,
			reinterpret_cast<const unsigned char*>(bytes.data()),
			bytes.size());

		jpeg_save_markers(&srcinfo, JPEG_COM, 0xFFFF);
		for (int m = 0; m < 16; m++) {
			jpeg_save_markers(&srcinfo, JPEG_APP0 + m, 0xFFFF);
		}

		jpeg_read_header(&srcinfo, true);
		const auto coefArrays = jpeg_read_coefficients(&srcinfo);
		jpeg_copy_critical_parameters(&srcinfo, &dstinfo);
		jpeg_simple_progression(&dstinfo);

		unsigned char* outbuffer = nullptr;
		long unsigned int outsize = 0;
		jpeg_mem_dest(&dstinfo, &outbuffer, &outsize);
		const auto outbufferGuard = gsl::finally([&] {
			free(outbuffer);
		});

		jpeg_write_coefficients(&dstinfo, coefArrays);

		for (jpeg_saved_marker_ptr marker = srcinfo.marker_list
			; marker != nullptr
			; marker = marker->next) {
			if (dstinfo.write_JFIF_header
				&& marker->marker == JPEG_APP0
				&& marker->data_length >= 5
				&& marker->data[0] == 0x4A
				&& marker->data[1] == 0x46
				&& marker->data[2] == 0x49
				&& marker->data[3] == 0x46
				&& marker->data[4] == 0)
				continue; // reject duplicate JFIF
			if (dstinfo.write_Adobe_marker
				&& marker->marker == JPEG_APP0 + 14
				&& marker->data_length >= 5
				&& marker->data[0] == 0x41
				&& marker->data[1] == 0x64
				&& marker->data[2] == 0x6F
				&& marker->data[3] == 0x62
				&& marker->data[4] == 0x65)
				continue; // reject duplicate Adobe
			jpeg_write_marker(
				&dstinfo,
				marker->marker,
				marker->data,
				marker->data_length);
		}

		jpeg_finish_compress(&dstinfo);
		jpeg_finish_decompress(&srcinfo);

		return QByteArray(reinterpret_cast<char*>(outbuffer), outsize);
	} catch (...) {
		return {};
	}
}

QByteArray ExpandInlineBytes(const QByteArray &bytes) {
	if (bytes.size() < 3 || bytes[0] != '\x01') {
		return QByteArray();
	}
	const char header[] = "\xff\xd8\xff\xe0\x00\x10\x4a\x46\x49"
		"\x46\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xdb\x00\x43\x00\x28\x1c"
		"\x1e\x23\x1e\x19\x28\x23\x21\x23\x2d\x2b\x28\x30\x3c\x64\x41\x3c\x37\x37"
		"\x3c\x7b\x58\x5d\x49\x64\x91\x80\x99\x96\x8f\x80\x8c\x8a\xa0\xb4\xe6\xc3"
		"\xa0\xaa\xda\xad\x8a\x8c\xc8\xff\xcb\xda\xee\xf5\xff\xff\xff\x9b\xc1\xff"
		"\xff\xff\xfa\xff\xe6\xfd\xff\xf8\xff\xdb\x00\x43\x01\x2b\x2d\x2d\x3c\x35"
		"\x3c\x76\x41\x41\x76\xf8\xa5\x8c\xa5\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
		"\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
		"\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
		"\xf8\xf8\xf8\xf8\xf8\xff\xc0\x00\x11\x08\x00\x00\x00\x00\x03\x01\x22\x00"
		"\x02\x11\x01\x03\x11\x01\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01"
		"\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08"
		"\x09\x0a\x0b\xff\xc4\x00\xb5\x10\x00\x02\x01\x03\x03\x02\x04\x03\x05\x05"
		"\x04\x04\x00\x00\x01\x7d\x01\x02\x03\x00\x04\x11\x05\x12\x21\x31\x41\x06"
		"\x13\x51\x61\x07\x22\x71\x14\x32\x81\x91\xa1\x08\x23\x42\xb1\xc1\x15\x52"
		"\xd1\xf0\x24\x33\x62\x72\x82\x09\x0a\x16\x17\x18\x19\x1a\x25\x26\x27\x28"
		"\x29\x2a\x34\x35\x36\x37\x38\x39\x3a\x43\x44\x45\x46\x47\x48\x49\x4a\x53"
		"\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66\x67\x68\x69\x6a\x73\x74\x75"
		"\x76\x77\x78\x79\x7a\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94\x95\x96"
		"\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4\xb5\xb6"
		"\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4\xd5\xd6"
		"\xd7\xd8\xd9\xda\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf1\xf2\xf3\xf4"
		"\xf5\xf6\xf7\xf8\xf9\xfa\xff\xc4\x00\x1f\x01\x00\x03\x01\x01\x01\x01\x01"
		"\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08"
		"\x09\x0a\x0b\xff\xc4\x00\xb5\x11\x00\x02\x01\x02\x04\x04\x03\x04\x07\x05"
		"\x04\x04\x00\x01\x02\x77\x00\x01\x02\x03\x11\x04\x05\x21\x31\x06\x12\x41"
		"\x51\x07\x61\x71\x13\x22\x32\x81\x08\x14\x42\x91\xa1\xb1\xc1\x09\x23\x33"
		"\x52\xf0\x15\x62\x72\xd1\x0a\x16\x24\x34\xe1\x25\xf1\x17\x18\x19\x1a\x26"
		"\x27\x28\x29\x2a\x35\x36\x37\x38\x39\x3a\x43\x44\x45\x46\x47\x48\x49\x4a"
		"\x53\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66\x67\x68\x69\x6a\x73\x74"
		"\x75\x76\x77\x78\x79\x7a\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94"
		"\x95\x96\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4"
		"\xb5\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4"
		"\xd5\xd6\xd7\xd8\xd9\xda\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf2\xf3\xf4"
		"\xf5\xf6\xf7\xf8\xf9\xfa\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00"
		"\x3f\x00";
	const char footer[] = "\xff\xd9";
	auto real = QByteArray(header, sizeof(header) - 1);
	real[164] = bytes[1];
	real[166] = bytes[2];
	return real
		+ bytes.mid(3)
		+ QByteArray::fromRawData(footer, sizeof(footer) - 1);
}

QImage FromInlineBytes(const QByteArray &bytes) {
	return Read({ .content = ExpandInlineBytes(bytes) }).image;
}

// Thanks TDLib for code.
QByteArray ExpandPathInlineBytes(const QByteArray &bytes) {
	auto result = QByteArray();
	result.reserve(3 * (bytes.size() + 1));
	result.append('M');
	for (unsigned char c : bytes) {
		if (c >= 128 + 64) {
			result.append("AACAAAAHAAALMAAAQASTAVAAAZ"
				"aacaaaahaaalmaaaqastava.az0123456789-,"[c - 128 - 64]);
		} else {
			if (c >= 128) {
				result.append(',');
			} else if (c >= 64) {
				result.append('-');
			}
			//char buffer[3] = { 0 }; // Unavailable on macOS < 10.15.
			//std::to_chars(buffer, buffer + 3, (c & 63));
			//result.append(buffer);
			result.append(QByteArray::number(c & 63));
		}
	}
	result.append('z');
	return result;
}

QPainterPath PathFromInlineBytes(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return QPainterPath();
	}
	const auto expanded = ExpandPathInlineBytes(bytes);
	const auto path = expanded.data(); // Allows checking for '\0' by index.
	auto position = 0;

	const auto isAlpha = [](char c) {
		c |= 0x20;
		return 'a' <= c && c <= 'z';
	};
	const auto isDigit = [](char c) {
		return '0' <= c && c <= '9';
	};
	const auto skipCommas = [&] {
		while (path[position] == ',') {
			++position;
		}
	};
	const auto getNumber = [&] {
		skipCommas();
		auto sign = 1;
		if (path[position] == '-') {
			sign = -1;
			++position;
		}
		double res = 0;
		while (isDigit(path[position])) {
			res = res * 10 + path[position++] - '0';
		}
		if (path[position] == '.') {
			++position;
			double mul = 0.1;
			while (isDigit(path[position])) {
				res += (path[position] - '0') * mul;
				mul *= 0.1;
				++position;
			}
		}
		return sign * res;
	};

	auto result = QPainterPath();
	auto x = 0.;
	auto y = 0.;
	while (path[position] != '\0') {
		skipCommas();
		if (path[position] == '\0') {
			break;
		}

		while (path[position] == 'm' || path[position] == 'M') {
			auto command = path[position++];
			do {
				if (command == 'm') {
					x += getNumber();
					y += getNumber();
				} else {
					x = getNumber();
					y = getNumber();
				}
				skipCommas();
			} while (path[position] != '\0' && !isAlpha(path[position]));
		}

		auto xStart = x;
		auto yStart = y;
		result.moveTo(xStart, yStart);
		auto haveLastEndControlPoint = false;
		auto xLastEndControlPoint = 0.;
		auto yLastEndControlPoint = 0.;
		auto isClosed = false;
		auto command = '-';
		while (!isClosed) {
			skipCommas();
			if (path[position] == '\0') {
				LOG(("SVG Error: Receive unclosed path: %1"
					).arg(QString::fromLatin1(path)));
				return QPainterPath();
			}
			if (isAlpha(path[position])) {
				command = path[position++];
			}
			switch (command) {
			case 'l':
			case 'L':
			case 'h':
			case 'H':
			case 'v':
			case 'V':
				if (command == 'l' || command == 'h') {
					x += getNumber();
				} else if (command == 'L' || command == 'H') {
					x = getNumber();
				}
				if (command == 'l' || command == 'v') {
					y += getNumber();
				} else if (command == 'L' || command == 'V') {
					y = getNumber();
				}
				result.lineTo(x, y);
				haveLastEndControlPoint = false;
				break;
			case 'C':
			case 'c':
			case 'S':
			case 's': {
				auto xStartControlPoint = 0.;
				auto yStartControlPoint = 0.;
				if (command == 'S' || command == 's') {
					if (haveLastEndControlPoint) {
						xStartControlPoint = 2 * x - xLastEndControlPoint;
						yStartControlPoint = 2 * y - yLastEndControlPoint;
					} else {
						xStartControlPoint = x;
						yStartControlPoint = y;
					}
				} else {
					xStartControlPoint = getNumber();
					yStartControlPoint = getNumber();
					if (command == 'c') {
						xStartControlPoint += x;
						yStartControlPoint += y;
					}
				}

				xLastEndControlPoint = getNumber();
				yLastEndControlPoint = getNumber();
				if (command == 'c' || command == 's') {
					xLastEndControlPoint += x;
					yLastEndControlPoint += y;
				}
				haveLastEndControlPoint = true;

				if (command == 'c' || command == 's') {
					x += getNumber();
					y += getNumber();
				} else {
					x = getNumber();
					y = getNumber();
				}
				result.cubicTo(
					xStartControlPoint,
					yStartControlPoint,
					xLastEndControlPoint,
					yLastEndControlPoint,
					x,
					y);
				break;
			}
			case 'm':
			case 'M':
				--position;
				[[fallthrough]];
			case 'z':
			case 'Z':
				if (x != xStart || y != yStart) {
					x = xStart;
					y = yStart;
					result.lineTo(x, y);
				}
				isClosed = true;
				break;
			default:
				LOG(("SVG Error: Receive invalid command %1 at pos %2: %3"
					).arg(command
					).arg(position
					).arg(QString::fromLatin1(path)));
				return QPainterPath();
			}
		}
	}
	return result;
}

} // namespace Images
