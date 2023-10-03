// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/side_bar_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace Ui {
namespace {

constexpr auto kMaxLabelLines = 3;
constexpr auto kPremiumLockedOpacity = 0.6;

} // namespace

SideBarButton::SideBarButton(
	not_null<QWidget*> parent,
	const QString &title,
	const style::SideBarButton &st)
: RippleButton(parent, st.ripple)
, _st(st)
, _arcPen(
	_st.textFg,
	// Use a divider to get 1.5.
	st::sideBarButtonLockPenWidth
		/ float64(st::sideBarButtonLockPenWidthDivider),
	Qt::SolidLine,
	Qt::SquareCap,
	Qt::RoundJoin)
, _text(_st.minTextWidth) {
	_text.setText(_st.style, title);
	setAttribute(Qt::WA_OpaquePaintEvent);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_iconCache = _iconCacheActive = QImage();
		_lock.iconCache = _lock.iconCacheActive = QImage();
		update();
	}, lifetime());
}

void SideBarButton::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_active = active;
	update();
}

void SideBarButton::setBadge(const QString &badge, bool muted) {
	if (_badge.toString() == badge && _badgeMuted == muted) {
		return;
	}
	_badge.setText(_st.badgeStyle, badge);
	_badgeMuted = muted;
	const auto width = badge.isEmpty()
		? 0
		: std::max(_st.badgeHeight, _badge.maxWidth() + 2 * _st.badgeSkip);
	if (_iconCacheBadgeWidth != width) {
		_iconCacheBadgeWidth = width;
		_iconCache = _iconCacheActive = QImage();
	}
	update();
}

void SideBarButton::setIconOverride(
		const style::icon *iconOverride,
		const style::icon *iconOverrideActive) {
	_iconOverride = iconOverride;
	_iconOverrideActive = iconOverrideActive;
	update();
}

void SideBarButton::setLocked(bool locked) {
	if (_lock.locked == locked) {
		return;
	}
	_lock.locked = locked;
	const auto charFiller = QChar('l');
	const auto count = std::ceil(st::sideBarButtonLockSize.width()
		/ float(_st.style.font->width(charFiller)));
	const auto filler = QString().fill(charFiller, count);
	const auto result = _lock.locked
		? (filler + _text.toString())
		: _text.toString().mid(count);
	_text.setText(_st.style, result);
	update();
}

bool SideBarButton::locked() const {
	return _lock.locked;
}

int SideBarButton::resizeGetHeight(int newWidth) {
	auto result = _st.minHeight;
	const auto text = std::min(
		_text.countHeight(newWidth - _st.textSkip * 2),
		_st.style.font->height * kMaxLabelLines);
	const auto add = text - _st.style.font->height;
	return result + std::max(add, 0);
}

void SideBarButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();

	const auto &bg = _active ? _st.textBgActive : _st.textBg;
	p.fillRect(clip, bg);

	RippleButton::paintRipple(p, 0, 0);

	if (_lock.locked) {
		p.setOpacity(kPremiumLockedOpacity);
	}

	const auto &icon = computeIcon();
	const auto x = (_st.iconPosition.x() < 0)
		? (width() - icon.width()) / 2
		: _st.iconPosition.x();
	const auto y = (_st.iconPosition.y() < 0)
		? (height() - icon.height()) / 2
		: _st.iconPosition.y();
	if (_iconCacheBadgeWidth) {
		validateIconCache();
		p.drawImage(x, y, _active ? _iconCacheActive : _iconCache);
	} else {
		icon.paint(p, x, y, width());
	}
	p.setPen(_active ? _st.textFgActive : _st.textFg);
	_text.drawElided(
		p,
		_st.textSkip,
		_st.textTop,
		(width() - 2 * _st.textSkip),
		kMaxLabelLines,
		style::al_top);

	if (_iconCacheBadgeWidth) {
		const auto desiredLeft = width() / 2 + _st.badgePosition.x();
		const auto x = std::min(
			desiredLeft,
			width() - _iconCacheBadgeWidth - st::defaultScrollArea.width);
		const auto y = _st.badgePosition.y();

		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush((_badgeMuted && !_active)
			? _st.badgeBgMuted
			: _st.badgeBg);
		const auto r = _st.badgeHeight / 2;
		p.drawRoundedRect(x, y, _iconCacheBadgeWidth, _st.badgeHeight, r, r);

		p.setPen(_st.badgeFg);
		_badge.draw(
			p,
			x + (_iconCacheBadgeWidth - _badge.maxWidth()) / 2,
			y + (_st.badgeHeight - _st.badgeStyle.font->height) / 2,
			width());
	}

	if (_lock.locked) {
		const auto lineWidths = _text.countLineWidths(
			width() - 2 * _st.textSkip,
			{ .reserve = kMaxLabelLines });
		if (lineWidths.empty()) {
			return;
		}
		validateLockIconCache();

		const auto &icon = _active ? _lock.iconCacheActive : _lock.iconCache;
		const auto size = icon.size() / style::DevicePixelRatio();
		p.translate(
			(width() - lineWidths.front()) / 2.,
			_st.textTop + (_st.style.font->height - size.height()) / 2.);
		p.setOpacity(1.);
		p.fillRect(QRect(QPoint(), size), bg);
		p.setOpacity(kPremiumLockedOpacity);
		p.translate(-_st.style.font->spacew / 2., 0);

		p.drawImage(0, 0, icon);
	}
}

const style::icon &SideBarButton::computeIcon() const {
	return _active
		? (_iconOverrideActive
			? *_iconOverrideActive
			: !_st.iconActive.empty()
			? _st.iconActive
			: _iconOverride
			? *_iconOverride
			: _st.icon)
		: _iconOverride
		? *_iconOverride
		: _st.icon;
}

void SideBarButton::validateIconCache() {
	Expects(_st.iconPosition.x() < 0);

	if (!(_active ? _iconCacheActive : _iconCache).isNull()) {
		return;
	}
	const auto &icon = computeIcon();
	auto image = QImage(
		icon.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		icon.paint(p, 0, 0, icon.width());
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(Qt::transparent);
		auto pen = QPen(Qt::transparent);
		pen.setWidth(2 * _st.badgeStroke);
		p.setPen(pen);
		auto hq = PainterHighQualityEnabler(p);
		const auto desiredLeft = (icon.width() / 2) + _st.badgePosition.x();
		const auto x = std::min(
			desiredLeft,
			(width()
				- _iconCacheBadgeWidth
				- st::defaultScrollArea.width
				- (width() / 2)
				+ (icon.width() / 2)));
		const auto top = (_st.iconPosition.y() >= 0)
			? _st.iconPosition.y()
			: (height() - icon.height()) / 2;
		const auto y = _st.badgePosition.y() - top;
		const auto r = _st.badgeHeight / 2.;
		p.drawRoundedRect(x, y, _iconCacheBadgeWidth, _st.badgeHeight, r, r);
	}
	(_active ? _iconCacheActive : _iconCache) = std::move(image);
}

void SideBarButton::validateLockIconCache() {
	if (!(_active ? _lock.iconCacheActive : _lock.iconCache).isNull()) {
		return;
	}
	const auto &size = st::sideBarButtonLockSize;
	auto image = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);

		const auto &arcOffset = st::sideBarButtonLockArcOffset;
		const auto arcWidth = size.width() - arcOffset * 2;
		const auto &arcHeight = st::sideBarButtonLockArcHeight;

		const auto blockRectWidth = size.width();
		const auto blockRectHeight = st::sideBarButtonLockBlockHeight;
		const auto blockRectTop = size.height() - blockRectHeight;

		const auto blockRect = QRectF(
			(size.width() - blockRectWidth) / 2,
			blockRectTop,
			blockRectWidth,
			blockRectHeight);
		const auto lineHeight = -(blockRect.y() - arcHeight)
			+ _arcPen.width() / 2.;

		p.setPen(Qt::NoPen);
		p.setBrush(_st.textFg);
		{
			p.drawRoundedRect(blockRect, 2, 2);
		}

		p.translate(size.width() - arcOffset, blockRect.y());

		p.setPen(_arcPen);
		const auto rLine = QLineF(0, 0, 0, lineHeight);
		const auto lLine = rLine.translated(-arcWidth, 0);
		p.drawLine(rLine);
		p.drawLine(lLine);

		p.drawArc(
			-arcWidth,
			-arcHeight - _arcPen.width() / 2.,
			arcWidth,
			arcHeight * 2,
			0,
			180 * 16);
	}
	(_active ? _lock.iconCacheActive : _lock.iconCache) = std::move(image);
}

} // namespace Ui
