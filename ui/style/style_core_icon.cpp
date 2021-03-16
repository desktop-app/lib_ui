// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_icon.h"

#include "ui/style/style_core.h"
#include "base/basic_types.h"

#include <QtGui/QPainter>

namespace style {
namespace internal {
namespace {

uint32 colorKey(QColor c) {
	return (((((uint32(c.red()) << 8) | uint32(c.green())) << 8) | uint32(c.blue())) << 8) | uint32(c.alpha());
}

base::flat_map<const IconMask*, QImage> iconMasks;
QMap<QPair<const IconMask*, uint32>, QPixmap> iconPixmaps;
OrderedSet<IconData*> iconData;

QImage createIconMask(const IconMask *mask, int scale) {
	auto maskImage = QImage::fromData(mask->data(), mask->size(), "PNG");
	maskImage.setDevicePixelRatio(DevicePixelRatio());
	Assert(!maskImage.isNull());

	// images are layouted like this:
	// 100x 200x
	// 300x
	const auto factor = DevicePixelRatio();
	const auto realscale = scale * factor;
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
		ConvertScale(width, scale) * factor,
		ConvertScale(height, scale) * factor,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

QSize readGeneratedSize(const IconMask *mask, int scale) {
	auto data = mask->data();
	auto size = mask->size();

	auto generateTag = qstr("GENERATE:");
	if (size > generateTag.size() && !memcmp(data, generateTag.data(), generateTag.size())) {
		size -= generateTag.size();
		data += generateTag.size();
		auto sizeTag = qstr("SIZE:");
		if (size > sizeTag.size() && !memcmp(data, sizeTag.data(), sizeTag.size())) {
			size -= sizeTag.size();
			data += sizeTag.size();
			auto baForStream = QByteArray::fromRawData(reinterpret_cast<const char*>(data), size);
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

MonoIcon::MonoIcon(const IconMask *mask, Color color, QPoint offset)
: _mask(mask)
, _color(std::move(color))
, _offset(offset) {
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

QPoint MonoIcon::offset() const {
	return _offset;
}

void MonoIcon::paint(QPainter &p, const QPoint &pos, int outerw) const {
	int w = width(), h = height();
	QPoint fullOffset = pos + offset();
	int partPosX = RightToLeft() ? (outerw - fullOffset.x() - w) : fullOffset.x();
	int partPosY = fullOffset.y();

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(partPosX, partPosY, w, h, _color);
	} else {
		p.drawPixmap(partPosX, partPosY, _pixmap);
	}
}

void MonoIcon::fill(QPainter &p, const QRect &rect) const {
	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, _color);
	} else {
		p.drawPixmap(rect, _pixmap, QRect(0, 0, _pixmap.width(), _pixmap.height()));
	}
}

void MonoIcon::paint(QPainter &p, const QPoint &pos, int outerw, QColor colorOverride) const {
	int w = width(), h = height();
	QPoint fullOffset = pos + offset();
	int partPosX = RightToLeft() ? (outerw - fullOffset.x() - w) : fullOffset.x();
	int partPosY = fullOffset.y();

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(partPosX, partPosY, w, h, colorOverride);
	} else {
		ensureColorizedImage(colorOverride);
		p.drawImage(partPosX, partPosY, _colorizedImage);
	}
}

void MonoIcon::fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, colorOverride);
	} else {
		ensureColorizedImage(colorOverride);
		p.drawImage(rect, _colorizedImage, _colorizedImage.rect());
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
		maskImage = createIconMask(_mask, Scale());
		size = maskImage.size() / DevicePixelRatio();
	}

	const auto w = size.width();
	const auto h = size.height();
	const auto fullOffset = pos + offset();
	const auto partPosX = RightToLeft() ? (outerw - fullOffset.x() - w) : fullOffset.x();
	const auto partPosY = fullOffset.y();

	if (!maskImage.isNull()) {
		auto colorizedImage = QImage(
			maskImage.size(),
			QImage::Format_ARGB32_Premultiplied);
		colorizeImage(maskImage, _color[paletteOverride]->c, &colorizedImage);
		p.drawImage(partPosX, partPosY, colorizedImage);
	} else {
		p.fillRect(partPosX, partPosY, w, h, _color[paletteOverride]);
	}
}

void MonoIcon::fill(
		QPainter &p,
		const QRect &rect,
		const style::palette &paletteOverride) const {
	auto size = readGeneratedSize(_mask, Scale());
	auto maskImage = QImage();
	if (size.isEmpty()) {
		maskImage = createIconMask(_mask, Scale());
		size = maskImage.size() / DevicePixelRatio();
	}
	if (!maskImage.isNull()) {
		auto colorizedImage = QImage(
			maskImage.size(),
			QImage::Format_ARGB32_Premultiplied);
		colorizeImage(maskImage, _color[paletteOverride]->c, &colorizedImage);
		p.drawImage(rect, colorizedImage, colorizedImage.rect());
	} else {
		p.fillRect(rect, _color[paletteOverride]);
	}
}

QImage MonoIcon::instance(QColor colorOverride, int scale) const {
	if (scale == kScaleAuto) {
		ensureLoaded();
		auto result = QImage(size() * DevicePixelRatio(), QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(DevicePixelRatio());
		if (_pixmap.isNull()) {
			result.fill(colorOverride);
		} else {
			colorizeImage(_maskImage, colorOverride, &result);
		}
		return result;
	}
	auto size = readGeneratedSize(_mask, scale);
	if (!size.isEmpty()) {
		auto result = QImage(size * DevicePixelRatio(), QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(DevicePixelRatio());
		result.fill(colorOverride);
		return result;
	}
	auto mask = createIconMask(_mask, scale);
	auto result = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(DevicePixelRatio());
	colorizeImage(mask, colorOverride, &result);
	return result;
}

void MonoIcon::ensureLoaded() const {
	if (_size.isValid()) {
		return;
	}
	if (!_maskImage.isNull()) {
		createCachedPixmap();
		return;
	}

	_size = readGeneratedSize(_mask, Scale());
	if (_size.isEmpty()) {
		auto i = iconMasks.find(_mask);
		if (i == iconMasks.cend()) {
			i = iconMasks.emplace(_mask, createIconMask(_mask, Scale())).first;
		}
		_maskImage = i->second;

		createCachedPixmap();
	}
}

void MonoIcon::ensureColorizedImage(QColor color) const {
	if (_colorizedImage.isNull()) _colorizedImage = QImage(_maskImage.size(), QImage::Format_ARGB32_Premultiplied);
	colorizeImage(_maskImage, color, &_colorizedImage);
}

void MonoIcon::createCachedPixmap() const {
	auto key = qMakePair(_mask, colorKey(_color->c));
	auto j = iconPixmaps.constFind(key);
	if (j == iconPixmaps.cend()) {
		auto image = colorizeImage(_maskImage, _color);
		j = iconPixmaps.insert(key, QPixmap::fromImage(std::move(image)));
	}
	_pixmap = j.value();
	_size = _pixmap.size() / DevicePixelRatio();
}

void IconData::created() {
	iconData.insert(this);
}

void IconData::fill(QPainter &p, const QRect &rect) const {
	if (_parts.empty()) return;

	auto partSize = _parts[0].size();
	for (const auto &part : _parts) {
		Assert(part.offset() == QPoint(0, 0));
		Assert(part.size() == partSize);
		part.fill(p, rect);
	}
}

void IconData::fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
	if (_parts.empty()) return;

	auto partSize = _parts[0].size();
	for (const auto &part : _parts) {
		Assert(part.offset() == QPoint(0, 0));
		Assert(part.size() == partSize);
		part.fill(p, rect, colorOverride);
	}
}

QImage IconData::instance(QColor colorOverride, int scale) const {
	Assert(_parts.size() == 1);
	auto &part = _parts[0];
	Assert(part.offset() == QPoint(0, 0));
	return part.instance(colorOverride, scale);
}

int IconData::width() const {
	if (_width < 0) {
		_width = 0;
		for (const auto &part : _parts) {
			accumulate_max(_width, part.offset().x() + part.width());
		}
	}
	return _width;
}

int IconData::height() const {
	if (_height < 0) {
		_height = 0;
		for (const auto &part : _parts) {
			accumulate_max(_height, part.offset().x() + part.height());
		}
	}
	return _height;
}

void resetIcons() {
	iconPixmaps.clear();
	for (const auto data : iconData) {
		data->reset();
	}
}

void destroyIcons() {
	iconData.clear();
	iconPixmaps.clear();
	iconMasks.clear();
}

} // namespace internal
} // namespace style
