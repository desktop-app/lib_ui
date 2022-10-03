// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/call_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

namespace Ui {
namespace {

constexpr auto kOuterBounceDuration = crl::time(100);

} // namespace

CallButton::CallButton(
	QWidget *parent,
	const style::CallButton &stFrom,
	const style::CallButton *stTo)
: RippleButton(parent, stFrom.button.ripple)
, _stFrom(&stFrom)
, _stTo(stTo) {
	init();
}

void CallButton::init() {
	resize(_stFrom->button.width, _stFrom->button.height);

	_bgMask = RippleAnimation::EllipseMask(QSize(_stFrom->bgSize, _stFrom->bgSize));
	_bgFrom = Ui::PixmapFromImage(style::colorizeImage(_bgMask, _stFrom->bg));
	if (_stTo) {
		Assert(_stFrom->button.width == _stTo->button.width);
		Assert(_stFrom->button.height == _stTo->button.height);
		Assert(_stFrom->bgPosition == _stTo->bgPosition);
		Assert(_stFrom->bgSize == _stTo->bgSize);

		_bg = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_bg.setDevicePixelRatio(style::DevicePixelRatio());
		_bgTo = Ui::PixmapFromImage(style::colorizeImage(_bgMask, _stTo->bg));
		_iconMixedMask = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixedMask.setDevicePixelRatio(style::DevicePixelRatio());
		_iconFrom = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconFrom.setDevicePixelRatio(style::DevicePixelRatio());
		_iconFrom.fill(Qt::black);
		{
			QPainter p(&_iconFrom);
			p.drawImage(
				(_stFrom->bgSize
					- _stFrom->button.icon.width()) / 2,
				(_stFrom->bgSize
					- _stFrom->button.icon.height()) / 2,
				_stFrom->button.icon.instance(Qt::white));
		}
		_iconTo = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconTo.setDevicePixelRatio(style::DevicePixelRatio());
		_iconTo.fill(Qt::black);
		{
			QPainter p(&_iconTo);
			p.drawImage(
				(_stTo->bgSize
					- _stTo->button.icon.width()) / 2,
				(_stTo->bgSize
					- _stTo->button.icon.height()) / 2,
				_stTo->button.icon.instance(Qt::white));
		}
		_iconMixed = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixed.setDevicePixelRatio(style::DevicePixelRatio());
	}
}

void CallButton::setOuterValue(float64 value) {
	if (_outerValue != value) {
		_outerAnimation.start([this] {
			if (_progress == 0. || _progress == 1.) {
				update();
			}
		}, _outerValue, value, kOuterBounceDuration);
		_outerValue = value;
	}
}

void CallButton::setText(rpl::producer<QString> text) {
	_label.create(this, std::move(text), _stFrom->label);
	_label->show();
	rpl::combine(
		sizeValue(),
		_label->sizeValue()
	) | rpl::start_with_next([=](QSize my, QSize label) {
		_label->moveToLeft(
			(my.width() - label.width()) / 2,
			my.height() - label.height(),
			my.width());
	}, _label->lifetime());
}

void CallButton::setProgress(float64 progress) {
	_progress = progress;
	update();
}

void CallButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	auto bgPosition = myrtlpoint(_stFrom->bgPosition);
	auto paintFrom = (_progress == 0.) || !_stTo;
	auto paintTo = !paintFrom && (_progress == 1.);

	auto outerValue = _outerAnimation.value(_outerValue);
	if (outerValue > 0.) {
		auto outerRadius = paintFrom ? _stFrom->outerRadius : paintTo ? _stTo->outerRadius : (_stFrom->outerRadius * (1. - _progress) + _stTo->outerRadius * _progress);
		auto outerPixels = outerValue * outerRadius;
		auto outerRect = QRectF(myrtlrect(bgPosition.x(), bgPosition.y(), _stFrom->bgSize, _stFrom->bgSize));
		outerRect = outerRect.marginsAdded(QMarginsF(outerPixels, outerPixels, outerPixels, outerPixels));

		PainterHighQualityEnabler hq(p);
		if (paintFrom) {
			p.setBrush(_stFrom->outerBg);
		} else if (paintTo) {
			p.setBrush(_stTo->outerBg);
		} else {
			p.setBrush(anim::brush(_stFrom->outerBg, _stTo->outerBg, _progress));
		}
		p.setPen(Qt::NoPen);
		p.drawEllipse(outerRect);
	}

	if (_bgOverride) {
		const auto &s = _stFrom->bgSize;
		p.setPen(Qt::NoPen);
		p.setBrush(*_bgOverride);

		PainterHighQualityEnabler hq(p);
		p.drawEllipse(QRect(_stFrom->bgPosition, QSize(s, s)));
	} else if (paintFrom) {
		p.drawPixmap(bgPosition, _bgFrom);
	} else if (paintTo) {
		p.drawPixmap(bgPosition, _bgTo);
	} else {
		style::colorizeImage(_bgMask, anim::color(_stFrom->bg, _stTo->bg, _progress), &_bg);
		p.drawImage(bgPosition, _bg);
	}

	auto rippleColorInterpolated = QColor();
	auto rippleColorOverride = &rippleColorInterpolated;
	if (_rippleOverride) {
		rippleColorOverride = &(*_rippleOverride);
	} else if (paintFrom) {
		rippleColorOverride = nullptr;
	} else if (paintTo) {
		rippleColorOverride = &_stTo->button.ripple.color->c;
	} else {
		rippleColorInterpolated = anim::color(_stFrom->button.ripple.color, _stTo->button.ripple.color, _progress);
	}
	paintRipple(p, _stFrom->button.rippleAreaPosition, rippleColorOverride);

	auto positionFrom = iconPosition(_stFrom);
	if (paintFrom) {
		const auto icon = &_stFrom->button.icon;
		icon->paint(p, positionFrom, width());
	} else {
		auto positionTo = iconPosition(_stTo);
		if (paintTo) {
			_stTo->button.icon.paint(p, positionTo, width());
		} else {
			mixIconMasks();
			style::colorizeImage(_iconMixedMask, st::callIconFg->c, &_iconMixed);
			p.drawImage(myrtlpoint(_stFrom->bgPosition), _iconMixed);
		}
	}
}

QPoint CallButton::iconPosition(not_null<const style::CallButton*> st) const {
	auto result = st->button.iconPosition;
	if (result.x() < 0) {
		result.setX((width() - st->button.icon.width()) / 2);
	}
	if (result.y() < 0) {
		result.setY((height() - st->button.icon.height()) / 2);
	}
	return result;
}

void CallButton::mixIconMasks() {
	_iconMixedMask.fill(Qt::black);

	Painter p(&_iconMixedMask);
	PainterHighQualityEnabler hq(p);
	auto paintIconMask = [this, &p](const QImage &mask, float64 angle) {
		auto skipFrom = _stFrom->bgSize / 2;
		p.translate(skipFrom, skipFrom);
		p.rotate(angle);
		p.translate(-skipFrom, -skipFrom);
		p.drawImage(0, 0, mask);
	};
	p.save();
	paintIconMask(_iconFrom, (_stFrom->angle - _stTo->angle) * _progress);
	p.restore();
	p.setOpacity(_progress);
	paintIconMask(_iconTo, (_stTo->angle - _stFrom->angle) * (1. - _progress));
}

void CallButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto over = isOver();
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (over != wasOver) {
		update();
	}
}

void CallButton::setColorOverrides(rpl::producer<CallButtonColors> &&colors) {
	std::move(
		colors
	) | rpl::start_with_next([=](const CallButtonColors &c) {
		_bgOverride = c.bg;
		_rippleOverride = c.ripple;
		update();
	}, lifetime());
}

void CallButton::setStyle(
		const style::CallButton &stFrom,
		const style::CallButton *stTo) {
	if (_stFrom == &stFrom && _stTo == stTo) {
		return;
	}
	_stFrom = &stFrom;
	_stTo = stTo;
	init();
	update();
}

QPoint CallButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _stFrom->button.rippleAreaPosition;
}

QImage CallButton::prepareRippleMask() const {
	return RippleAnimation::EllipseMask(QSize(_stFrom->button.rippleAreaSize, _stFrom->button.rippleAreaSize));
}

} // namespace Ui
