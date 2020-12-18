// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/call_mute_button.h"

#include "base/flat_map.h"
#include "ui/abstract_button.h"
#include "ui/paint/blobs.h"
#include "ui/painter.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/labels.h"

#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtCore/QtMath>

namespace Ui {
namespace {

using Radiuses = Paint::Blob::Radiuses;

constexpr auto kMaxLevel = 1.;

constexpr auto kLevelDuration = 100. + 500. * 0.33;

constexpr auto kScaleBig = 0.807 - 0.1;
constexpr auto kScaleSmall = 0.704 - 0.1;

constexpr auto kScaleBigMin = 0.878;
constexpr auto kScaleSmallMin = 0.926;

constexpr auto kScaleBigMax = (float)(kScaleBigMin + kScaleBig);
constexpr auto kScaleSmallMax = (float)(kScaleSmallMin + kScaleSmall);

constexpr auto kMainRadiusFactor = (float)(48. / 57.);

constexpr auto kGlowPaddingFactor = 1.2;
constexpr auto kGlowMinScale = 0.6;
constexpr auto kGlowAlpha = 150;

constexpr auto kOverrideColorBgAlpha = 76;
constexpr auto kOverrideColorRippleAlpha = 50;

constexpr auto kShiftDuration = crl::time(300);
constexpr auto kSwitchStateDuration = crl::time(120);

// Switch state from Connecting animation.
constexpr auto kSwitchRadialDuration = crl::time(225);
constexpr auto kSwitchCirclelDuration = crl::time(275);
constexpr auto kBlobsScaleEnterDuration = crl::time(400);
constexpr auto kSwitchStateFromConnectingDuration = kSwitchRadialDuration
	+ kSwitchCirclelDuration
	+ kBlobsScaleEnterDuration;

constexpr auto kRadialEndPartAnimation = float(kSwitchRadialDuration)
	/ kSwitchStateFromConnectingDuration;
constexpr auto kBlobsWidgetPartAnimation = 1. - kRadialEndPartAnimation;
constexpr auto kFillCirclePartAnimation = float(kSwitchCirclelDuration)
	/ (kSwitchCirclelDuration + kBlobsScaleEnterDuration);
constexpr auto kBlobPartAnimation = float(kBlobsScaleEnterDuration)
	/ (kSwitchCirclelDuration + kBlobsScaleEnterDuration);

constexpr auto kOverlapProgressRadialHide = 1.2;

auto MuteBlobs() {
	return std::vector<Paint::Blobs::BlobData>{
		{
			.segmentsCount = 9,
			.minScale = kScaleSmallMin / kScaleSmallMax,
			.minRadius = st::callMuteMinorBlobMinRadius
				* kScaleSmallMax
				* kMainRadiusFactor,
			.maxRadius = st::callMuteMinorBlobMaxRadius
				* kScaleSmallMax
				* kMainRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
		{
			.segmentsCount = 12,
			.minScale = kScaleBigMin / kScaleBigMax,
			.minRadius = st::callMuteMajorBlobMinRadius
				* kScaleBigMax
				* kMainRadiusFactor,
			.maxRadius = st::callMuteMajorBlobMaxRadius
				* kScaleBigMax
				* kMainRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
	};
}

auto Colors() {
	using Vector = std::vector<QColor>;
	using Colors = anim::gradient_colors;
	return base::flat_map<CallMuteButtonType, Colors>{
		{
			CallMuteButtonType::ForceMuted,
			Colors(QGradientStops{
				{ .0, st::groupCallForceMuted1->c },
				{ .5, st::groupCallForceMuted2->c },
				{ 1., st::groupCallForceMuted3->c } })
		},
		{
			CallMuteButtonType::Active,
			Colors(Vector{ st::groupCallLive1->c, st::groupCallLive2->c })
		},
		{
			CallMuteButtonType::Connecting,
			Colors(st::callIconBg->c)
		},
		{
			CallMuteButtonType::Muted,
			Colors(Vector{ st::groupCallMuted1->c, st::groupCallMuted2->c })
		},
	};
}

bool IsMuted(CallMuteButtonType type) {
	return (type != CallMuteButtonType::Active);
}

bool IsConnecting(CallMuteButtonType type) {
	return (type == CallMuteButtonType::Connecting);
}

bool IsInactive(CallMuteButtonType type) {
	return IsConnecting(type) || (type == CallMuteButtonType::ForceMuted);
}

auto Clamp(float64 value) {
	return std::clamp(value, 0., 1.);
}

} // namespace

class BlobsWidget final : public RpWidget {
public:
	BlobsWidget(
		not_null<RpWidget*> parent,
		rpl::producer<bool> &&hideBlobs);

	void setLevel(float level);
	void setBlobBrush(QBrush brush);
	void setGlowBrush(QBrush brush);

	[[nodiscard]] QRectF innerRect() const;

	[[nodiscard]] float64 switchConnectingProgress() const;
	void setSwitchConnectingProgress(float64 progress);

private:
	void init();

	Paint::Blobs _blobs;

	const float _circleRaidus;
	QBrush _blobBrush;
	QBrush _glowBrush;
	int _center = 0;
	QRectF _circleRect;

	float64 _switchConnectingProgress = 0.;

	crl::time _blobsLastTime = 0;
	crl::time _blobsHideLastTime = 0;

	float64 _blobsScaleEnter = 0.;
	crl::time _blobsScaleLastTime = 0;

	bool _hideBlobs = true;

	Animations::Basic _animation;

};

BlobsWidget::BlobsWidget(
	not_null<RpWidget*> parent,
	rpl::producer<bool> &&hideBlobs)
: RpWidget(parent)
, _blobs(MuteBlobs(), kLevelDuration, kMaxLevel)
, _circleRaidus(st::callMuteMainBlobMinRadius * kMainRadiusFactor)
, _blobBrush(Qt::transparent)
, _glowBrush(Qt::transparent)
, _blobsLastTime(crl::now())
, _blobsScaleLastTime(crl::now()) {
	init();

	std::move(
		hideBlobs
	) | rpl::start_with_next([=](bool hide) {
		if (_hideBlobs != hide) {
			const auto now = crl::now();
			if ((now - _blobsScaleLastTime) >= kBlobsScaleEnterDuration) {
				_blobsScaleLastTime = now;
			}
			_hideBlobs = hide;
		}
		if (hide) {
			setLevel(0.);
		}
		_blobsHideLastTime = hide ? crl::now() : 0;
		if (!hide && !_animation.animating()) {
			_animation.start();
		}
	}, lifetime());
}

void BlobsWidget::init() {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	{
		const auto s = _blobs.maxRadius() * 2 * kGlowPaddingFactor;
		resize(s, s);
	}

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_center = size.width() / 2;

		{
			const auto &r = _circleRaidus;
			const auto left = (size.width() - r * 2.) / 2.;
			const auto add = st::callConnectingRadial.thickness / 2;
			_circleRect = QRectF(left, left, r * 2, r * 2).marginsAdded(
				style::margins(add, add, add, add));
		}
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		PainterHighQualityEnabler hq(p);

		// Glow.
		const auto s = kGlowMinScale
			+ (1. - kGlowMinScale) * _blobs.currentLevel();
		p.translate(_center, _center);
		p.scale(s, s);
		p.translate(-_center, -_center);
		p.fillRect(rect(), _glowBrush);
		p.resetTransform();

		// Blobs.
		p.translate(_center, _center);
		const auto scale = (_switchConnectingProgress > 0.)
			? anim::easeOutBack(
				1.,
				_blobsScaleEnter * (1. - Clamp(
					_switchConnectingProgress / kBlobPartAnimation)))
			: _blobsScaleEnter;
		_blobs.paint(p, _blobBrush, scale);

		// Main circle.
		p.translate(-_center, -_center);
		p.setPen(Qt::NoPen);
		p.setBrush(_blobBrush);
		p.drawEllipse(_circleRect);

		if (_switchConnectingProgress > 0.) {
			p.resetTransform();

			const auto circleProgress =
				Clamp(_switchConnectingProgress - kBlobPartAnimation)
					/ kFillCirclePartAnimation;

			const auto mF = (_circleRect.width() / 2) * (1. - circleProgress);
			const auto cutOutRect = _circleRect.marginsRemoved(
				QMarginsF(mF, mF, mF, mF));

			p.setPen(Qt::NoPen);
			p.setBrush(st::callConnectingRadial.color);
			p.setOpacity(circleProgress);
			p.drawEllipse(_circleRect);

			p.setOpacity(1.);
			p.setBrush(st::callIconBg);

			p.save();
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.drawEllipse(cutOutRect);
			p.restore();

			p.drawEllipse(cutOutRect);
		}
	}, lifetime());

	_animation.init([=](crl::time now) {
		if (const auto &last = _blobsHideLastTime; (last > 0)
			&& (now - last >= kBlobsScaleEnterDuration)) {
			_animation.stop();
			return false;
		}
		_blobs.updateLevel(now - _blobsLastTime);
		_blobsLastTime = now;

		const auto dt = Clamp(
			(now - _blobsScaleLastTime) / float64(kBlobsScaleEnterDuration));
		_blobsScaleEnter = _hideBlobs
			? (1. - anim::linear(1., dt))
			: anim::easeOutBack(1., dt);

		update();
		return true;
	});
	shownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (shown) {
			_animation.start();
		} else {
			_animation.stop();
		}
	}, lifetime());
}

QRectF BlobsWidget::innerRect() const {
	return _circleRect;
}

void BlobsWidget::setBlobBrush(QBrush brush) {
	if (_blobBrush == brush) {
		return;
	}
	_blobBrush = brush;
}

void BlobsWidget::setGlowBrush(QBrush brush) {
	if (_glowBrush == brush) {
		return;
	}
	_glowBrush = brush;
}

void BlobsWidget::setLevel(float level) {
	if (_blobsHideLastTime) {
		 return;
	}
	_blobs.setLevel(level);
}

float64 BlobsWidget::switchConnectingProgress() const {
	return _switchConnectingProgress;
}

void BlobsWidget::setSwitchConnectingProgress(float64 progress) {
	_switchConnectingProgress = progress;
}

CallMuteButton::CallMuteButton(
	not_null<RpWidget*> parent,
	rpl::producer<bool> &&hideBlobs,
	CallMuteButtonState initial)
: _state(initial)
, _st(st::callMuteButtonActive)
, _blobs(base::make_unique_q<BlobsWidget>(
	parent,
	rpl::combine(
		rpl::single(anim::Disabled()) | rpl::then(anim::Disables()),
		std::move(hideBlobs),
		_state.value(
		) | rpl::map([](const CallMuteButtonState &state) {
			return IsInactive(state.type);
		})
	) | rpl::map([](bool animDisabled, bool hide, bool isBadState) {
		return isBadState || !(!animDisabled && !hide);
	})))
, _content(base::make_unique_q<AbstractButton>(parent))
, _label(base::make_unique_q<FlatLabel>(
	parent,
	_state.value(
	) | rpl::map([](const CallMuteButtonState &state) {
		return state.text;
	}),
	_st.label))
, _sublabel(base::make_unique_q<FlatLabel>(
	parent,
	_state.value(
	) | rpl::map([](const CallMuteButtonState &state) {
		return state.subtext;
	}),
	st::callMuteButtonSublabel))
, _radial(nullptr)
, _colors(Colors())
, _crossLineMuteAnimation(st::callMuteCrossLine) {
	init();
}

void CallMuteButton::init() {
	_content->resize(_st.button.width, _st.button.height);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_crossLineMuteAnimation.invalidate();
	}, lifetime());

	// Label text.
	_label->show();
	rpl::combine(
		_content->geometryValue(),
		_label->sizeValue()
	) | rpl::start_with_next([=](QRect my, QSize size) {
		updateLabelGeometry(my, size);
	}, _label->lifetime());
	_label->setAttribute(Qt::WA_TransparentForMouseEvents);

	_sublabel->show();
	rpl::combine(
		_content->geometryValue(),
		_sublabel->sizeValue()
	) | rpl::start_with_next([=](QRect my, QSize size) {
		updateSublabelGeometry(my, size);
	}, _sublabel->lifetime());
	_sublabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	_radialShowProgress.value(
	) | rpl::start_with_next([=](float64 value) {
		if (((value == 0.) || anim::Disabled()) && _radial) {
			_radial->stop();
			_radial = nullptr;
			return;
		}
		if ((value > 0.) && !anim::Disabled() && !_radial) {
			_radial = std::make_unique<InfiniteRadialAnimation>(
				[=] { _content->update(); },
				st::callConnectingRadial);
			_radial->start();
		}

		if (value == 1.) {
			_lastRadialState = std::nullopt;
		} else {
			if (_radial && !_lastRadialState.has_value()) {
				_lastRadialState = _radial->computeState();
			}
		}
	}, lifetime());

	// State type.
	const auto previousType =
		lifetime().make_state<CallMuteButtonType>(_state.current().type);
	setHandleMouseState(HandleMouseState::Disabled);

	const auto blobsInner = [&] {
		// The point of the circle at 45 degrees.
		const auto w = _blobs->innerRect().width();
		const auto mF = (1 - std::cos(M_PI / 4.)) * (w / 2.);
		return _blobs->innerRect().marginsRemoved(QMarginsF(mF, mF, mF, mF));
	}();

	auto linearGradients = anim::linear_gradients<CallMuteButtonType>(
		_colors,
		QPointF(blobsInner.x() + blobsInner.width(), blobsInner.y()),
		QPointF(blobsInner.x(), blobsInner.y() + blobsInner.height()));

	auto glowColors = [&] {
		auto copy = _colors;
		for (auto &[type, stops] : copy) {
			auto firstColor = IsInactive(type)
				? st::groupCallBg->c
				: stops.stops[0].second;
			firstColor.setAlpha(kGlowAlpha);
			stops.stops = QGradientStops{
				{ 0., std::move(firstColor) },
				{ 1., QColor(Qt::transparent) }
			};
		}
		return copy;
	}();
	auto glows = anim::radial_gradients<CallMuteButtonType>(
		std::move(glowColors),
		blobsInner.center(),
		_blobs->width() / 2);

	_state.value(
	) | rpl::map([](const CallMuteButtonState &state) {
		return state.type;
	}) | rpl::start_with_next([=](CallMuteButtonType type) {
		const auto previous = *previousType;
		*previousType = type;

		const auto mouseState = HandleMouseStateFromType(type);
		setHandleMouseState(HandleMouseState::Disabled);
		if (mouseState != HandleMouseState::Enabled) {
			setHandleMouseState(mouseState);
		}

		const auto fromConnecting = IsConnecting(previous);
		const auto toConnecting = IsConnecting(type);

		const auto crossFrom = IsMuted(previous) ? 0. : 1.;
		const auto crossTo = IsMuted(type) ? 0. : 1.;

		const auto radialShowFrom = fromConnecting ? 1. : 0.;
		const auto radialShowTo = toConnecting ? 1. : 0.;

		const auto from = (_switchAnimation.animating() && !fromConnecting)
			? (1. - _switchAnimation.value(0.))
			: 0.;
		const auto to = 1.;

		auto callback = [=](float64 value) {
			const auto brushProgress = fromConnecting ? 1. : value;
			_blobs->setBlobBrush(QBrush(
				linearGradients.gradient(previous, type, brushProgress)));
			_blobs->setGlowBrush(QBrush(
				glows.gradient(previous, type, value)));
			_blobs->update();

			const auto crossProgress = (crossFrom == crossTo)
				? crossTo
				: anim::interpolateF(crossFrom, crossTo, value);
			if (crossProgress != _crossLineProgress) {
				_crossLineProgress = crossProgress;
				_content->update(_muteIconRect);
			}

			const auto radialShowProgress = (radialShowFrom == radialShowTo)
				? radialShowTo
				: anim::interpolateF(radialShowFrom, radialShowTo, value);
			if (radialShowProgress != _radialShowProgress.current()) {
				_radialShowProgress = radialShowProgress;
				_blobs->setSwitchConnectingProgress(Clamp(
					radialShowProgress / kBlobsWidgetPartAnimation));
			}

			overridesColors(previous, type, value);

			if (value == to) {
				setHandleMouseState(mouseState);
			}
		};

		_switchAnimation.stop();
		const auto duration = (1. - from) * ((fromConnecting || toConnecting)
			? kSwitchStateFromConnectingDuration
			: kSwitchStateDuration);
		_switchAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	// Icon rect.
	_content->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto &icon = _st.button.icon;
		const auto &pos = _st.button.iconPosition;

		_muteIconRect = QRect(
			(pos.x() < 0) ? ((size.width() - icon.width()) / 2) : pos.x(),
			(pos.y() < 0) ? ((size.height() - icon.height()) / 2) : pos.y(),
			icon.width(),
			icon.height());
	}, lifetime());

	// Paint.
	_content->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		Painter p(_content);

		_crossLineMuteAnimation.paint(
			p,
			_muteIconRect.topLeft(),
			1. - _crossLineProgress);

		if (_lastRadialState.has_value() && _switchAnimation.animating()) {
			const auto radialProgress = (1. - _radialShowProgress.current())
				/ kRadialEndPartAnimation;

			auto r = *_lastRadialState;
			r.arcLength = anim::interpolate(
				r.arcLength,
				-RadialState::kFull,
				Clamp(radialProgress));

			const auto opacity = (radialProgress > kOverlapProgressRadialHide)
				? 0.
				: _blobs->switchConnectingProgress();
			p.setOpacity(opacity);
			InfiniteRadialAnimation::Draw(
				p,
				r,
				_st.bgPosition,
				st::callConnectingRadial.size,
				_content->width(),
				QPen(st::callConnectingRadial.color),
				st::callConnectingRadial.thickness);
		} else if (_radial) {
			_radial->draw(
				p,
				_st.bgPosition,
				_content->width());
		}
	}, _content->lifetime());
}

void CallMuteButton::updateLabelsGeometry() {
	updateLabelGeometry(_content->geometry(), _label->size());
	updateSublabelGeometry(_content->geometry(), _sublabel->size());
}

void CallMuteButton::updateLabelGeometry(QRect my, QSize size) {
	_label->moveToLeft(
		my.x() + (my.width() - size.width()) / 2 + _labelShakeShift,
		my.y() + my.height() - size.height() - st::callMuteButtonSublabelSkip,
		my.width());
}

void CallMuteButton::updateSublabelGeometry(QRect my, QSize size) {
	_sublabel->moveToLeft(
		my.x() + (my.width() - size.width()) / 2 + _labelShakeShift,
		my.y() + my.height() - size.height(),
		my.width());
}

void CallMuteButton::shake() {
	if (_shakeAnimation.animating()) {
		return;
	}
	const auto update = [=] {
		const auto fullProgress = _shakeAnimation.value(1.) * 6;
		const auto segment = std::clamp(int(std::floor(fullProgress)), 0, 5);
		const auto part = fullProgress - segment;
		const auto from = (segment == 0)
			? 0.
			: (segment == 1 || segment == 3 || segment == 5)
			? 1.
			: -1.;
		const auto to = (segment == 0 || segment == 2 || segment == 4)
			? 1.
			: (segment == 1 || segment == 3)
			? -1.
			: 0.;
		const auto shift = from * (1. - part) + to * part;
		_labelShakeShift = int(std::round(shift * st::shakeShift));
		updateLabelsGeometry();
	};
	_shakeAnimation.start(
		update,
		0.,
		1.,
		kShiftDuration);
}

CallMuteButton::HandleMouseState CallMuteButton::HandleMouseStateFromType(
		CallMuteButtonType type) {
	switch (type) {
	case CallMuteButtonType::Active:
	case CallMuteButtonType::Muted:
		return HandleMouseState::Enabled;
	case CallMuteButtonType::Connecting:
		return HandleMouseState::Disabled;
	case CallMuteButtonType::ForceMuted:
		return HandleMouseState::Blocked;
	}
	Unexpected("Type in HandleMouseStateFromType.");
}

void CallMuteButton::setState(const CallMuteButtonState &state) {
	_state = state;
}

void CallMuteButton::setLevel(float level) {
	_level = level;
	_blobs->setLevel(level);
}

rpl::producer<Qt::MouseButton> CallMuteButton::clicks() const {
	return _content->clicks();
}

QSize CallMuteButton::innerSize() const {
	return innerGeometry().size();
}

QRect CallMuteButton::innerGeometry() const {
	const auto &skip = _st.outerRadius;
	return QRect(
		_content->x(),
		_content->y(),
		_content->width() - 2 * skip,
		_content->width() - 2 * skip);
}

void CallMuteButton::moveInner(QPoint position) {
	const auto &skip = _st.outerRadius;
	_content->move(position - QPoint(skip, skip));

	{
		const auto offset = QPoint(
			(_blobs->width() - _content->width()) / 2,
			(_blobs->height() - _content->width()) / 2);
		_blobs->move(_content->pos() - offset);
	}
}

void CallMuteButton::setVisible(bool visible) {
	_content->setVisible(visible);
	_blobs->setVisible(visible);
}

void CallMuteButton::raise() {
	_blobs->raise();
	_content->raise();
}

void CallMuteButton::lower() {
	_content->lower();
	_blobs->lower();
}

void CallMuteButton::setHandleMouseState(HandleMouseState state) {
	if (_handleMouseState == state) {
		return;
	}
	_handleMouseState = state;
	const auto handle = (_handleMouseState != HandleMouseState::Disabled);
	const auto pointer = (_handleMouseState == HandleMouseState::Enabled);
	_content->setAttribute(Qt::WA_TransparentForMouseEvents, !handle);
	_content->setPointerCursor(pointer);
}

void CallMuteButton::overridesColors(
		CallMuteButtonType fromType,
		CallMuteButtonType toType,
		float64 progress) {
	const auto forceMutedToConnecting = [](CallMuteButtonType &type) {
		if (type == CallMuteButtonType::ForceMuted) {
			type = CallMuteButtonType::Connecting;
		}
	};
	forceMutedToConnecting(toType);
	forceMutedToConnecting(fromType);
	const auto toInactive = IsInactive(toType);
	const auto fromInactive = IsInactive(fromType);
	if (toInactive && (progress == 1)) {
		_colorOverrides.fire({ std::nullopt, std::nullopt });
		return;
	}
	auto from = _colors.find(fromType)->second.stops[0].second;
	auto to = _colors.find(toType)->second.stops[0].second;
	auto fromRipple = from;
	auto toRipple = to;
	if (!toInactive) {
		toRipple.setAlpha(kOverrideColorRippleAlpha);
		to.setAlpha(kOverrideColorBgAlpha);
	}
	if (!fromInactive) {
		fromRipple.setAlpha(kOverrideColorRippleAlpha);
		from.setAlpha(kOverrideColorBgAlpha);
	}
	const auto resultBg = anim::color(from, to, progress);
	const auto resultRipple = anim::color(fromRipple, toRipple, progress);
	_colorOverrides.fire({ resultBg, resultRipple });
}

rpl::producer<CallButtonColors> CallMuteButton::colorOverrides() const {
	return _colorOverrides.events();
}

rpl::lifetime &CallMuteButton::lifetime() {
	return _blobs->lifetime();
}

CallMuteButton::~CallMuteButton() = default;

} // namespace Ui
