// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flags.h"
#include "ui/rect_part.h"
#include "ui/style/style_core.h"

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

enum class ImageRoundRadius {
	None,
	Large,
	Small,
	Ellipse,
};

namespace Images {

[[nodiscard]] QPixmap PixmapFast(QImage &&image);
[[nodiscard]] QImage BlurLargeImage(QImage &&image, int radius);
[[nodiscard]] QImage DitherImage(const QImage &image);

[[nodiscard]] QImage GenerateGradient(
	QSize size,
	const std::vector<QColor> &colors, // colors.size() <= 4.
	int rotation = 0,
	float progress = 1.f);

[[nodiscard]] QImage GenerateLinearGradient(
	QSize size,
	const std::vector<QColor> &colors,
	int rotation = 0);

[[nodiscard]] QImage GenerateShadow(
	int height,
	int topAlpha,
	int bottomAlpha,
	QColor color = QColor(0, 0, 0));

inline constexpr auto kTopLeft = 0;
inline constexpr auto kTopRight = 1;
inline constexpr auto kBottomLeft = 2;
inline constexpr auto kBottomRight = 3;

struct CornersMaskRef {
	CornersMaskRef() = default;
	explicit CornersMaskRef(gsl::span<const QImage, 4> masks)
	: p{ &masks[0], &masks[1], &masks[2], &masks[3] } {
	}
	explicit CornersMaskRef(std::array<const QImage, 4> masks)
	: p{ &masks[0], &masks[1], &masks[2], &masks[3] } {
	}
	explicit CornersMaskRef(gsl::span<const QImage*, 4> masks)
	: p{ masks[0], masks[1], masks[2], masks[3] } {
	}
	explicit CornersMaskRef(std::array<const QImage*, 4> masks)
	: p{ masks[0], masks[1], masks[2], masks[3] } {
	}

	[[nodiscard]] bool empty() const {
		return !p[0] && !p[1] && !p[2] && !p[3];
	}

	std::array<const QImage*, 4> p{};

	friend inline constexpr std::strong_ordering operator<=>(
			CornersMaskRef a,
			CornersMaskRef b) noexcept {
		for (auto i = 0; i != 4; ++i) {
			if (a.p[i] < b.p[i]) {
				return std::strong_ordering::less;
			} else if (a.p[i] > b.p[i]) {
				return std::strong_ordering::greater;
			}
		}
		return std::strong_ordering::equal;
	}
	friend inline constexpr bool operator==(
		CornersMaskRef a,
		CornersMaskRef b) noexcept = default;
};

[[nodiscard]] const std::array<QImage, 4> &CornersMask(
	ImageRoundRadius radius);
[[nodiscard]] std::array<QImage, 4> PrepareCorners(
	ImageRoundRadius radius,
	const style::color &color);

[[nodiscard]] std::array<QImage, 4> CornersMask(int radius);
[[nodiscard]] QImage EllipseMask(QSize size, double ratio = style::DevicePixelRatio());

[[nodiscard]] std::array<QImage, 4> PrepareCorners(
	int radius,
	const style::color &color);

[[nodiscard]] QByteArray UnpackGzip(const QByteArray &bytes);

// Try to read images up to 64MB.
inline constexpr auto kReadBytesLimit = 64 * 1024 * 1024;
inline constexpr auto kReadMaxArea = 12'032 * 9'024;

struct ReadArgs {
	QString path;
	QByteArray content;
	QSize maxSize;
	bool gzipSvg = false;
	bool forceOpaque = false;
	bool returnContent = false;
};
struct ReadResult {
	QImage image;
	QByteArray content;
	QByteArray format;
	bool animated = false;
};
[[nodiscard]] ReadResult Read(ReadArgs &&args);

enum class Option {
	None                  = 0,
	FastTransform         = (1 << 0),
	Blur                  = (1 << 1),
	RoundCircle           = (1 << 2),
	RoundLarge            = (1 << 3),
	RoundSmall            = (1 << 4),
	RoundSkipTopLeft      = (1 << 5),
	RoundSkipTopRight     = (1 << 6),
	RoundSkipBottomLeft   = (1 << 7),
	RoundSkipBottomRight  = (1 << 8),
	Colorize              = (1 << 9),
	TransparentBackground = (1 << 10),
};
using Options = base::flags<Option>;
inline constexpr auto is_flag_type(Option) { return true; };

[[nodiscard]] Options RoundOptions(
	ImageRoundRadius radius,
	RectParts corners = RectPart::AllCorners);

[[nodiscard]] QImage Round(
	QImage &&image,
	CornersMaskRef mask,
	QRect target = QRect());

[[nodiscard]] QImage Blur(QImage &&image, bool ignoreAlpha = false);
[[nodiscard]] QImage Round(
	QImage &&image,
	ImageRoundRadius radius,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
[[nodiscard]] QImage Round(
	QImage &&image,
	gsl::span<const QImage, 4> cornerMasks,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
[[nodiscard]] QImage Round(
	QImage &&image,
	Options options,
	QRect target = QRect());

[[nodiscard]] QImage Circle(QImage &&image, QRect target = QRect());
[[nodiscard]] QImage Colored(QImage &&image, style::color add);
[[nodiscard]] QImage Colored(QImage &&image, QColor add);
[[nodiscard]] QImage Opaque(QImage &&image);

struct PrepareArgs {
	const style::color *colored = nullptr;
	Options options;
	QSize outer;

	[[nodiscard]] PrepareArgs blurred() const {
		auto result = *this;
		result.options |= Option::Blur;
		return result;
	}
};

[[nodiscard]] QImage Prepare(
	QImage image,
	int w,
	int h,
	const PrepareArgs &args);

[[nodiscard]] inline QImage Prepare(
		QImage image,
		int w,
		const PrepareArgs &args) {
	return Prepare(std::move(image), w, 0, args);
}

[[nodiscard]] inline QImage Prepare(
		QImage image,
		QSize size,
		const PrepareArgs &args) {
	return Prepare(std::move(image), size.width(), size.height(), args);
}

[[nodiscard]] bool IsProgressiveJpeg(const QByteArray &bytes);
[[nodiscard]] QByteArray MakeProgressiveJpeg(const QByteArray &bytes);

[[nodiscard]] QByteArray ExpandInlineBytes(const QByteArray &bytes);
[[nodiscard]] QImage FromInlineBytes(const QByteArray &bytes);
[[nodiscard]] QPainterPath PathFromInlineBytes(const QByteArray &bytes);

} // namespace Images
