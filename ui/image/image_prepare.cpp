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
#include <QtGui/QImageReader>
#include <QtSvg/QSvgRenderer>

namespace Images {
namespace {

// They should be smaller.
constexpr auto kMaxGzipFileSize = 5 * 1024 * 1024;

TG_FORCE_INLINE uint64 blurGetColors(const uchar *p) {
	return (uint64)p[0] + ((uint64)p[1] << 16) + ((uint64)p[2] << 32) + ((uint64)p[3] << 48);
}

const QImage &circleMask(QSize size) {
	uint64 key = (uint64(uint32(size.width())) << 32)
		| uint64(uint32(size.height()));

	static auto masks = base::flat_map<uint64, QImage>();
	const auto i = masks.find(key);
	if (i != end(masks)) {
		return i->second;
	}
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
	return masks.emplace(key, std::move(mask)).first->second;
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
		: exact.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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
		result.image = prepareOpaque(std::move(result.image));
	}
	return result;
}

QImage prepareBlur(QImage img) {
	if (img.isNull()) {
		return img;
	}
	const auto ratio = img.devicePixelRatio();
	const auto fmt = img.format();
	if (fmt != QImage::Format_RGB32 && fmt != QImage::Format_ARGB32_Premultiplied) {
		img = std::move(img).convertToFormat(QImage::Format_ARGB32_Premultiplied);
		img.setDevicePixelRatio(ratio);
	}

	uchar *pix = img.bits();
	if (pix) {
		int w = img.width(), h = img.height();
		const int radius = 3;
		const int r1 = radius + 1;
		const int div = radius * 2 + 1;
		const int stride = w * 4;
		if (radius < 16 && div < w && div < h && stride <= w * 4) {
			bool withalpha = img.hasAlphaChannel();
			if (withalpha) {
				QImage imgsmall(w, h, img.format());
				{
					QPainter p(&imgsmall);
					PainterHighQualityEnabler hq(p);

					p.setCompositionMode(QPainter::CompositionMode_Source);
					p.fillRect(0, 0, w, h, Qt::transparent);
					p.drawImage(QRect(radius, radius, w - 2 * radius, h - 2 * radius), img, QRect(0, 0, w, h));
				}
				imgsmall.setDevicePixelRatio(ratio);
				auto was = img;
				img = std::move(imgsmall);
				imgsmall = QImage();
				Assert(!img.isNull());

				pix = img.bits();
				if (!pix) return was;
			}
			uint64 *rgb = new uint64[w * h];

			int x, y, i;

			int yw = 0;
			const int we = w - r1;
			for (y = 0; y < h; y++) {
				uint64 cur = blurGetColors(&pix[yw]);
				uint64 rgballsum = -radius * cur;
				uint64 rgbsum = cur * ((r1 * (r1 + 1)) >> 1);

				for (i = 1; i <= radius; i++) {
					uint64 cur = blurGetColors(&pix[yw + i * 4]);
					rgbsum += cur * (r1 - i);
					rgballsum += cur;
				}

				x = 0;

#define update(start, middle, end) \
rgb[y * w + x] = (rgbsum >> 4) & 0x00FF00FF00FF00FFLL; \
rgballsum += blurGetColors(&pix[yw + (start) * 4]) - 2 * blurGetColors(&pix[yw + (middle) * 4]) + blurGetColors(&pix[yw + (end) * 4]); \
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

			delete[] rgb;
		}
	}
	return img;
}

QImage BlurLargeImage(QImage image, int radius) {
	const auto width = image.width();
	const auto height = image.height();
	if (width <= radius || height <= radius || radius < 1) {
		return image;
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
	return image;
}

[[nodiscard]] QImage DitherImage(QImage image) {
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
	}
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
	gradient.setStops(QGradientStops{
		{ 0.0, colors[0] },
		{ 1.0, colors[1] }
	});
	p.fillRect(QRect(QPoint(), size), QBrush(std::move(gradient)));
	p.end();

	return result;
}

void prepareCircle(QImage &img) {
	Assert(!img.isNull());

	img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	Assert(!img.isNull());

	QPainter p(&img);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(
		QRect(QPoint(), img.size() / img.devicePixelRatio()),
		circleMask(img.size()));
}

void prepareRound(
		QImage &image,
		QImage *cornerMasks,
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
	if (targetWidth < 2 * cornerWidth || targetHeight < 2 * cornerHeight) {
		return;
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
}

void prepareRound(
		QImage &image,
		ImageRoundRadius radius,
		RectParts corners,
		QRect target) {
	if (!static_cast<int>(corners)) {
		return;
	} else if (radius == ImageRoundRadius::Ellipse) {
		Assert((corners & RectPart::AllCorners) == RectPart::AllCorners);
		Assert(target.isNull());
		prepareCircle(image);
		return;
	}
	Assert(!image.isNull());

	image = std::move(image).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	Assert(!image.isNull());

	auto masks = CornersMask(radius);
	prepareRound(image, masks.data(), corners, target);
}

QImage prepareColored(style::color add, QImage image) {
	return prepareColored(add->c, std::move(image));
}

QImage prepareColored(QColor add, QImage image) {
	const auto format = image.format();
	if (format != QImage::Format_RGB32 && format != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
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
			int b = pix[i], g = pix[i + 1], r = pix[i + 2], a = pix[i + 3], aca = a * ca;
			pix[i + 0] = uchar(b + ((aca * (cb - b)) >> 16));
			pix[i + 1] = uchar(g + ((aca * (cg - g)) >> 16));
			pix[i + 2] = uchar(r + ((aca * (cr - r)) >> 16));
			pix[i + 3] = uchar(a + ((aca * (0xFF - a)) >> 16));
		}
	}
	return image;
}

QImage prepareOpaque(QImage image) {
	if (image.hasAlphaChannel()) {
		image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
		auto ints = reinterpret_cast<uint32*>(image.bits());
		auto bg = anim::shifted(st::imageBgTransparent->c);
		auto width = image.width();
		auto height = image.height();
		auto addPerLine = (image.bytesPerLine() / sizeof(uint32)) - width;
		for (auto y = 0; y != height; ++y) {
			for (auto x = 0; x != width; ++x) {
				auto components = anim::shifted(*ints);
				*ints++ = anim::unshifted(components * 256 + bg * (256 - anim::getAlpha(components)));
			}
			ints += addPerLine;
		}
	}
	return image;
}

QImage prepare(QImage img, int w, int h, Images::Options options, int outerw, int outerh, const style::color *colored) {
	Assert(!img.isNull());
	if (options & Images::Option::Blurred) {
		img = prepareBlur(std::move(img));
		Assert(!img.isNull());
	}
	if (w <= 0 || (w == img.width() && (h <= 0 || h == img.height()))) {
	} else if (h <= 0) {
		img = img.scaledToWidth(w, (options & Images::Option::Smooth) ? Qt::SmoothTransformation : Qt::FastTransformation);
		Assert(!img.isNull());
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, (options & Images::Option::Smooth) ? Qt::SmoothTransformation : Qt::FastTransformation);
		Assert(!img.isNull());
	}
	if (outerw > 0 && outerh > 0) {
		const auto pixelRatio = style::DevicePixelRatio();
		outerw *= pixelRatio;
		outerh *= pixelRatio;
		if (outerw != w || outerh != h) {
			img.setDevicePixelRatio(pixelRatio);
			auto result = QImage(outerw, outerh, QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(pixelRatio);
			if (options & Images::Option::TransparentBackground) {
				result.fill(Qt::transparent);
			}
			{
				QPainter p(&result);
				if (!(options & Images::Option::TransparentBackground)) {
					if (w < outerw || h < outerh) {
						p.fillRect(0, 0, result.width(), result.height(), st::imageBg);
					}
				}
				p.drawImage((result.width() - img.width()) / (2 * pixelRatio), (result.height() - img.height()) / (2 * pixelRatio), img);
			}
			img = result;
			Assert(!img.isNull());
		}
	}
	auto corners = [](Images::Options options) {
		return ((options & Images::Option::RoundedTopLeft) ? RectPart::TopLeft : RectPart::None)
			| ((options & Images::Option::RoundedTopRight) ? RectPart::TopRight : RectPart::None)
			| ((options & Images::Option::RoundedBottomLeft) ? RectPart::BottomLeft : RectPart::None)
			| ((options & Images::Option::RoundedBottomRight) ? RectPart::BottomRight : RectPart::None);
	};
	if (options & Images::Option::Circled) {
		prepareCircle(img);
		Assert(!img.isNull());
	} else if (options & Images::Option::RoundedLarge) {
		prepareRound(img, ImageRoundRadius::Large, corners(options));
		Assert(!img.isNull());
	} else if (options & Images::Option::RoundedSmall) {
		prepareRound(img, ImageRoundRadius::Small, corners(options));
		Assert(!img.isNull());
	}
	if (options & Images::Option::Colored) {
		Assert(colored != nullptr);
		img = prepareColored(*colored, std::move(img));
	}
	img.setDevicePixelRatio(style::DevicePixelRatio());
	return img;
}

} // namespace Images
