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

#include "zlib.h"
#include <QtCore/QFile>
#include <QtCore/QBuffer>
#include <QtCore/QMutex>
#include <QtGui/QImageReader>
#include <QtSvg/QSvgRenderer>

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

const QImage &CircleMask(QSize size) {
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

	auto mask = QImage(
		size,
		QImage::Format_ARGB32_Premultiplied);
	mask.fill(Qt::transparent);
	{
		QPainter p(&mask);
		PainterHighQualityEnabler hq(p);
		p.setBrush(Qt::white);
		p.setPen(Qt::NoPen);
		p.drawEllipse(QRect(QPoint(), size));
	}

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
	if (!reader.read(&result.image) || result.image.isNull()) {
		return {};
	}
	result.animated = reader.supportsAnimation()
		&& (reader.imageCount() > 1);
	result.format = reader.format().toLower();
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
	if (args.forceOpaque
		&& result.format != qstr("jpg")
		&& result.format != qstr("jpeg")) {
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

QImage Blur(QImage &&image) {
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
	const auto withalpha = image.hasAlphaChannel();
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
		target = QRect(QPoint(), image.size());
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
		CircleMask(target.size()));
	p.end();

	return std::move(image);
}

QImage Round(
		QImage &&image,
		gsl::span<const QImage, 4> cornerMasks,
		RectParts corners,
		QRect target) {
	if (target.isNull()) {
		target = QRect(QPoint(), image.size());
	} else {
		Assert(QRect(QPoint(), image.size()).contains(target));
	}
	auto cornerWidth = cornerMasks[0].width();
	auto cornerHeight = cornerMasks[0].height();
	auto targetWidth = target.width();
	auto targetHeight = target.height();
	if (targetWidth < cornerWidth || targetHeight < cornerHeight) {
		return std::move(image);
	}

	// We need to detach image first (if it is shared), before we
	// count some offsets using QImage::bytesPerLine etc, because
	// bytesPerLine may change on detach, this leads to crashes:
	// Real image bytesPerLine is smaller than the one we use for offsets.
	auto ints = reinterpret_cast<uint32*>(image.bits());

	constexpr auto imageIntsPerPixel = 1;
	auto imageIntsPerLine = (image.bytesPerLine() >> 2);
	Assert(image.depth() == static_cast<int>((imageIntsPerPixel * sizeof(uint32)) << 3));
	Assert(image.bytesPerLine() == (imageIntsPerLine << 2));
	auto intsTopLeft = ints + target.x() + target.y() * imageIntsPerLine;
	auto intsTopRight = ints + target.x() + targetWidth - cornerWidth + target.y() * imageIntsPerLine;
	auto intsBottomLeft = ints + target.x() + (target.y() + targetHeight - cornerHeight) * imageIntsPerLine;
	auto intsBottomRight = ints + target.x() + targetWidth - cornerWidth + (target.y() + targetHeight - cornerHeight) * imageIntsPerLine;
	auto maskCorner = [&](uint32 *imageInts, const QImage &mask) {
		auto maskWidth = mask.width();
		auto maskHeight = mask.height();
		auto maskBytesPerPixel = (mask.depth() >> 3);
		auto maskBytesPerLine = mask.bytesPerLine();
		auto maskBytesAdded = maskBytesPerLine - maskWidth * maskBytesPerPixel;
		auto maskBytes = mask.constBits();
		Assert(maskBytesAdded >= 0);
		Assert(mask.depth() == (maskBytesPerPixel << 3));
		auto imageIntsAdded = imageIntsPerLine - maskWidth * imageIntsPerPixel;
		Assert(imageIntsAdded >= 0);
		for (auto y = 0; y != maskHeight; ++y) {
			for (auto x = 0; x != maskWidth; ++x) {
				auto opacity = static_cast<anim::ShiftedMultiplier>(*maskBytes) + 1;
				*imageInts = anim::unshifted(anim::shifted(*imageInts) * opacity);
				maskBytes += maskBytesPerPixel;
				imageInts += imageIntsPerPixel;
			}
			maskBytes += maskBytesAdded;
			imageInts += imageIntsAdded;
		}
	};
	if (corners & RectPart::TopLeft) maskCorner(intsTopLeft, cornerMasks[0]);
	if (corners & RectPart::TopRight) maskCorner(intsTopRight, cornerMasks[1]);
	if (corners & RectPart::BottomLeft) maskCorner(intsBottomLeft, cornerMasks[2]);
	if (corners & RectPart::BottomRight) maskCorner(intsBottomRight, cornerMasks[3]);

	return std::move(image);
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

	image = std::move(image).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
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
		const auto ca = int(add.alphaF() * 0xFF);
		const auto cr = int(add.redF() * 0xFF);
		const auto cg = int(add.greenF() * 0xFF);
		const auto cb = int(add .blueF() * 0xFF);
		const auto w = image.width();
		const auto h = image.height();
		const auto size = w * h * 4;
		for (auto i = index_type(); i != size; i += 4) {
			const auto b = pix[i];
			const auto g = pix[i + 1];
			const auto r = pix[i + 2];
			const auto a = pix[i + 3];
			const auto aca = a * ca;
			pix[i + 0] = uchar(b + ((aca * (cb - b)) >> 16));
			pix[i + 1] = uchar(g + ((aca * (cg - g)) >> 16));
			pix[i + 2] = uchar(r + ((aca * (cr - r)) >> 16));
			pix[i + 3] = uchar(a + ((aca * (0xFF - a)) >> 16));
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

} // namespace Images
