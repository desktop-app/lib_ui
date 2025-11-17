// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_icon.h"

#include "ui/style/style_core_palette.h"
#include "ui/style/style_core.h"
#include "ui/painter.h"
#include "base/basic_types.h"

#include <QtCore/QMutex>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

namespace style {
namespace internal {
namespace {

[[nodiscard]] uint32 ColorKey(QColor c) {
	return (uint32(c.red()) << 24)
		| (uint32(c.green()) << 16)
		| (uint32(c.blue()) << 8)
		| uint32(c.alpha());
}

base::flat_map<const IconMask*, QImage> IconMasks;
QMutex IconMasksMutex;

base::flat_map<QPair<const IconMask*, uint32>, QPixmap> iconPixmaps;
base::flat_set<IconData*> iconData;

[[nodiscard]] QImage CreateIconMask(
		not_null<const IconMask*> mask,
		int scale,
		bool ignoreDpr = false) {
	const auto ratio = ignoreDpr ? 1 : DevicePixelRatio();
	const auto realscale = scale * ratio;

	auto data = QByteArray::fromRawData(
		reinterpret_cast<const char*>(mask->data()),
		mask->size());
	if (data.startsWith("SVG:")) {
		auto size = QSize();
		data = QByteArray::fromRawData(
			data.constData() + 4,
			data.size() - 4);
		if (data.startsWith("SIZE:")) {
			data = QByteArray::fromRawData(
				data.constData() + 5,
				data.size() - 5);

			QDataStream stream(data);
			stream.setVersion(QDataStream::Qt_5_1);

			qint32 width = 0, height = 0;
			stream >> width >> height;
			Assert(stream.status() == QDataStream::Ok);

			size = QSize(width, height);
			data = QByteArray::fromRawData(
				data.constData() + 8,
				data.size() - 8);
		}
		auto svg = QSvgRenderer(data);
		Assert(svg.isValid());
		if (size.isEmpty()) {
			size = svg.defaultSize();
		}
		const auto width = ConvertScale(size.width(), scale);
		const auto height = ConvertScale(size.height(), scale);
		auto maskImage = QImage(
			QSize(width, height) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		maskImage.fill(Qt::transparent);
		maskImage.setDevicePixelRatio(ratio);
		auto p = QPainter(&maskImage);
		auto hq = PainterHighQualityEnabler(p);
		svg.render(&p, QRectF(0, 0, width, height));
		return maskImage;
	}

	auto maskImage = QImage::fromData(mask->data(), mask->size(), "PNG");
	maskImage.setDevicePixelRatio(ratio);
	Assert(!maskImage.isNull());

	// images are laid out like this:
	// 100x 200x
	// 300x
	const auto width = maskImage.width() / 3;
	const auto height = maskImage.height() / 5;
	const auto one = QRect(0, 0, width, height);
	const auto two = QRect(width, 0, width * 2, height * 2);
	const auto three = QRect(0, height * 2, width * 3, height * 3);
	if (realscale == 100) {
		return maskImage.copy(one);
	} else if (realscale == 200) {
		return maskImage.copy(two);
	} else if (realscale == 300) {
		return maskImage.copy(three);
	}
	return maskImage.copy(
		(realscale > 200) ? three : two
	).scaled(
		ConvertScale(width, scale) * ratio,
		ConvertScale(height, scale) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

[[nodiscard]] QImage ResolveIconMask(not_null<const IconMask*> mask) {
	QMutexLocker lock(&IconMasksMutex);
	if (const auto i = IconMasks.find(mask); i != end(IconMasks)) {
		return i->second;
	}
	return IconMasks.emplace(
		mask,
		CreateIconMask(mask, Scale())
	).first->second;
}

[[nodiscard]] QSize readGeneratedSize(
		const IconMask *mask,
		int scale,
		bool ignoreDpr = false) {
	auto data = mask->data();
	auto size = mask->size();

	auto generateTag = qstr("GENERATE:");
	if (size > generateTag.size()
		&& !memcmp(data, generateTag.data(), generateTag.size())) {
		size -= generateTag.size();
		data += generateTag.size();
		auto sizeTag = qstr("SIZE:");
		if (size > sizeTag.size()
			&& !memcmp(data, sizeTag.data(), sizeTag.size())) {
			size -= sizeTag.size();
			data += sizeTag.size();
			auto baForStream = QByteArray::fromRawData(
				reinterpret_cast<const char*>(data),
				size);
			QDataStream stream(baForStream);
			stream.setVersion(QDataStream::Qt_5_1);

			qint32 width = 0, height = 0;
			stream >> width >> height;
			Assert(stream.status() == QDataStream::Ok);

			return QSize(
				ConvertScale(width, scale),
				ConvertScale(height, scale));
		} else {
			Unexpected("Bad data in generated icon!");
		}
	}
	return QSize();
}

} // namespace

MonoIcon::MonoIcon(const MonoIcon &other, const style::palette &palette)
: _mask(other._mask)
, _color(
	palette.colorAtIndex(
		style::main_palette::indexOfColor(other._color)))
, _padding(other._padding) {
}

MonoIcon::MonoIcon(const IconMask *mask, Color color, QMargins padding)
: _mask(mask)
, _color(std::move(color))
, _padding(padding) {
}

void MonoIcon::reset() const {
	_pixmap = QPixmap();
	_size = QSize();
}

int MonoIcon::width() const {
	ensureLoaded();
	return _size.width();
}

int MonoIcon::height() const {
	ensureLoaded();
	return _size.height();
}

QSize MonoIcon::size() const {
	ensureLoaded();
	return _size;
}

QSize MonoIcon::inner() const {
	ensureLoaded();
	return _size.shrunkBy(_padding);
}

void MonoIcon::paint(QPainter &p, const QPoint &pos, int outerw) const {
	const auto partPosX = RightToLeft()
		? (outerw - pos.x() - width() + _padding.right())
		: (pos.x() + _padding.left());
	const auto partPosY = pos.y() + _padding.top();

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(QRect(QPoint(partPosX, partPosY), inner()), _color);
	} else {
		p.drawPixmap(partPosX, partPosY, _pixmap);
	}
}

void MonoIcon::fill(QPainter &p, const QRect &rect) const {
	Expects(_padding.isNull());

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, _color);
	} else {
		p.drawPixmap(rect, _pixmap);
	}
}

void MonoIcon::paint(
		QPainter &p,
		const QPoint &pos,
		int outerw,
		QColor colorOverride) const {
	const auto partPosX = RightToLeft()
		? (outerw - pos.x() - width() + _padding.right())
		: (pos.x() + _padding.left());
	const auto partPosY = pos.y() + _padding.top();

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(
			QRect(QPoint(partPosX, partPosY), inner()),
			colorOverride);
	} else {
		ensureColorizedImage(colorOverride);
		p.drawImage(partPosX, partPosY, _colorizedImage);
	}
}

void MonoIcon::fill(
		QPainter &p,
		const QRect &rect,
		QColor colorOverride) const {
	Expects(_padding.isNull());

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, colorOverride);
	} else {
		ensureColorizedImage(colorOverride);
		p.drawImage(rect, _colorizedImage);
	}
}

void MonoIcon::paint(
		QPainter &p,
		const QPoint &pos,
		int outerw,
		const style::palette &paletteOverride) const {
	auto size = readGeneratedSize(_mask, Scale());
	auto maskImage = QImage();
	if (size.isEmpty()) {
		maskImage = CreateIconMask(_mask, Scale());
		size = maskImage.size() / DevicePixelRatio();
	}

	const auto partPosX = RightToLeft()
		? (outerw - pos.x() - width() + _padding.right())
		: (pos.x() + _padding.left());
	const auto partPosY = pos.y() + _padding.top();

	if (!maskImage.isNull()) {
		auto colorized = QImage(
			maskImage.size(),
			QImage::Format_ARGB32_Premultiplied);
		colorizeImage(maskImage, _color[paletteOverride]->c, &colorized);
		p.drawImage(partPosX, partPosY, colorized);
	} else {
		p.fillRect(
			QRect(QPoint(partPosX, partPosY), inner()),
			_color[paletteOverride]);
	}
}

void MonoIcon::fill(
		QPainter &p,
		const QRect &rect,
		const style::palette &paletteOverride) const {
	Expects(_padding.isNull());

	auto size = readGeneratedSize(_mask, Scale());
	auto maskImage = QImage();
	if (size.isEmpty()) {
		maskImage = CreateIconMask(_mask, Scale());
		size = maskImage.size() / DevicePixelRatio();
	}
	if (!maskImage.isNull()) {
		auto colorized = QImage(
			maskImage.size(),
			QImage::Format_ARGB32_Premultiplied);
		colorizeImage(maskImage, _color[paletteOverride]->c, &colorized);
		p.drawImage(rect, colorized);
	} else {
		p.fillRect(rect, _color[paletteOverride]);
	}
}

QImage MonoIcon::instance(
		QColor colorOverride,
		int scale,
		bool ignoreDpr) const {
	Expects(_padding.isNull() || scale == kScaleAuto);

	if (scale == kScaleAuto) {
		ensureLoaded();
		const auto ratio = DevicePixelRatio();
		auto result = QImage(
			size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		if (_pixmap.isNull()) {
			if (_padding.isNull()) {
				result.fill(colorOverride);
			} else {
				result.fill(Qt::transparent);
				auto p = QPainter(&result);
				p.fillRect(
					QRect(QPoint(), size()).marginsRemoved(_padding),
					colorOverride);
			}
		} else {
			if (!_padding.isNull()) {
				result.fill(Qt::transparent);
			}
			colorizeImage(
				_maskImage,
				colorOverride,
				&result,
				QRect(),
				QPoint(_padding.left(), _padding.top()) * ratio);
		}
		return result;
	}
	const auto ratio = ignoreDpr ? 1 : DevicePixelRatio();
	auto size = readGeneratedSize(_mask, scale, ignoreDpr);
	if (!size.isEmpty()) {
		auto result = QImage(
			size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		result.fill(colorOverride);
		return result;
	}
	auto mask = CreateIconMask(_mask, scale, ignoreDpr);
	auto result = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	colorizeImage(mask, colorOverride, &result);
	return result;
}

void MonoIcon::ensureLoaded() const {
	if (_size.isValid()) {
		return;
	} else if (!_maskImage.isNull()) {
		createCachedPixmap();
		return;
	}

	_size = readGeneratedSize(_mask, Scale());
	if (!_size.isEmpty()) {
		_size = _size.grownBy(_padding);
	} else {
		_maskImage = ResolveIconMask(_mask);
		createCachedPixmap();
	}
}

void MonoIcon::ensureColorizedImage(QColor color) const {
	if (_colorizedImage.isNull()) {
		_colorizedImage = QImage(
			_maskImage.size(),
			QImage::Format_ARGB32_Premultiplied);
	}
	colorizeImage(_maskImage, color, &_colorizedImage);
}

void MonoIcon::createCachedPixmap() const {
	auto key = qMakePair(_mask, ColorKey(_color->c));
	auto j = iconPixmaps.find(key);
	if (j == end(iconPixmaps)) {
		auto image = colorizeImage(_maskImage, _color);
		j = iconPixmaps.emplace(
			key,
			QPixmap::fromImage(std::move(image))).first;
	}
	_pixmap = j->second;
	_size = (_pixmap.size() / DevicePixelRatio()).grownBy(_padding);
}

IconData::IconData(const IconData &other, const style::palette &palette) {
	created();
	_parts.reserve(other._parts.size());
	for (const auto &part : other._parts) {
		_parts.push_back(MonoIcon(part, palette));
	}
}

void IconData::created() {
	iconData.emplace(this);
}

IconData::~IconData() {
	iconData.remove(this);
}

void IconData::fill(QPainter &p, const QRect &rect) const {
	if (_parts.empty()) return;

	auto partSize = _parts[0].size();
	for (const auto &part : _parts) {
		Assert(part.size() == partSize);
		part.fill(p, rect);
	}
}

void IconData::fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
	if (_parts.empty()) return;

	auto partSize = _parts[0].size();
	for (const auto &part : _parts) {
		Assert(part.size() == partSize);
		part.fill(p, rect, colorOverride);
	}
}

QImage IconData::instance(
		QColor colorOverride,
		int scale,
		bool ignoreDpr) const {
	Expects(_parts.size() == 1);

	return _parts[0].instance(colorOverride, scale, ignoreDpr);
}

int IconData::width() const {
	if (_width < 0) {
		_width = 0;
		for (const auto &part : _parts) {
			accumulate_max(_width, part.width());
		}
	}
	return _width;
}

int IconData::height() const {
	if (_height < 0) {
		_height = 0;
		for (const auto &part : _parts) {
			accumulate_max(_height, part.height());
		}
	}
	return _height;
}

Icon Icon::withPalette(const style::palette &palette) const {
	Expects(_data != nullptr);

	auto result = Icon(Qt::Uninitialized);
	result._data = new IconData(*_data, palette);
	result._owner = true;
	return result;
}

void Icon::paintInCenter(QPainter &p, const QRectF &outer) const {
	const auto dx = outer.x() + (outer.width() - width()) / 2.;
	const auto dy = outer.y() + (outer.height() - height()) / 2.;
	p.translate(dx, dy);
	_data->paint(p, QPoint(), outer.x() * 2. + outer.width());
	p.translate(-dx, -dy);
}

void Icon::paintInCenter(
		QPainter &p,
		const QRectF &outer,
		QColor override) const {
	const auto dx = outer.x() + (outer.width() - width()) / 2;
	const auto dy = outer.y() + (outer.height() - height()) / 2;
	p.translate(dx, dy);
	_data->paint(p, QPoint(), outer.x() * 2 + outer.width(), override);
	p.translate(-dx, -dy);
}

void ResetIcons() {
	iconPixmaps.clear();
	for (const auto data : iconData) {
		data->reset();
	}
}

void DestroyIcons() {
	iconData.clear();
	iconPixmaps.clear();

	QMutexLocker lock(&IconMasksMutex);
	IconMasks.clear();
}

} // namespace internal
} // namespace style
