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
[[nodiscard]] QImage BlurLargeImage(QImage image, int radius);
[[nodiscard]] QImage DitherImage(QImage image);

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

[[nodiscard]] const std::array<QImage, 4> &CornersMask(
	ImageRoundRadius radius);
[[nodiscard]] std::array<QImage, 4> PrepareCorners(
	ImageRoundRadius radius,
	const style::color &color);

[[nodiscard]] std::array<QImage, 4> CornersMask(int radius);
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

QImage prepareBlur(QImage image);
void prepareRound(
	QImage &image,
	ImageRoundRadius radius,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
void prepareRound(
	QImage &image,
	gsl::span<const QImage, 4> cornerMasks,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
void prepareCircle(QImage &image);
QImage prepareColored(style::color add, QImage image);
QImage prepareColored(QColor add, QImage image);
QImage prepareOpaque(QImage image);

enum class Option {
	None                  = 0,
	Smooth                = (1 << 0),
	Blurred               = (1 << 1),
	Circled               = (1 << 2),
	RoundedLarge          = (1 << 3),
	RoundedSmall          = (1 << 4),
	RoundedTopLeft        = (1 << 5),
	RoundedTopRight       = (1 << 6),
	RoundedBottomLeft     = (1 << 7),
	RoundedBottomRight    = (1 << 8),
	RoundedAll            = (None
		| RoundedTopLeft
		| RoundedTopRight
		| RoundedBottomLeft
		| RoundedBottomRight),
	Colored               = (1 << 9),
	TransparentBackground = (1 << 10),
};
using Options = base::flags<Option>;
inline constexpr auto is_flag_type(Option) { return true; };

QImage prepare(QImage img, int w, int h, Options options, int outerw, int outerh, const style::color *colored = nullptr);

} // namespace Images
