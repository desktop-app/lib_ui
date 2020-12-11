// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/call_mute_button.h"

#include "base/flat_map.h"
#include "ui/abstract_button.h"
#include "ui/effects/gradient.h"
#include "ui/effects/radial_animation.h"
#include "ui/paint/blobs.h"
#include "ui/painter.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/labels.h"

#include "styles/palette.h"
#include "styles/style_widgets.h"

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

constexpr auto kMainRadiusFactor = (float)(50. / 57.);

constexpr auto kGlowPaddingFactor = 1.2;
constexpr auto kGlowMinScale = 0.6;
constexpr auto kGlowAlpha = 150;

constexpr auto kOverrideColorBgAlpha = 76;
constexpr auto kOverrideColorRippleAlpha = 50;

constexpr auto kSwitchStateDuration = 120;

auto MuteBlobs() {
	return std::vector<Paint::Blobs::BlobData>{
		{
			.segmentsCount = 6,
			.minScale = 1.,
			.minRadius = st::callMuteMainBlobMinRadius
				* kMainRadiusFactor,
			.maxRadius = st::callMuteMainBlobMaxRadius
				* kMainRadiusFactor,
			.speedScale = .4,
			.alpha = 1.,
		},
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
	return base::flat_map<CallMuteButtonType, Vector>{
		{
			CallMuteButtonType::ForceMuted,
			Vector{ st::callIconBg->c, st::callIconBg->c }
		},
		{
			CallMuteButtonType::Active,
			Vector{ st::groupCallLive1->c, st::groupCallLive2->c }
		},
		{
			CallMuteButtonType::Connecting,
			Vector{ st::callIconBg->c, st::callIconBg->c }
		},
		{
			CallMuteButtonType::Muted,
			Vector{ st::groupCallMuted1->c, st::groupCallMuted2->c }
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

} // namespace

class BlobsWidget final : public RpWidget {
public:
	BlobsWidget(
		not_null<RpWidget*> parent,
		rpl::producer<bool> &&hideBlobs);

	void setLevel(float level);
	void setBlobBrush(QBrush brush);
	void setGlowBrush(QBrush brush);

	[[nodiscard]] QRect innerRect() const;

private:
	void init();

	Paint::Blobs _blobs;

	QBrush _blobBrush;
	QBrush _glowBrush;
	int _center = 0;
	QRect _inner;

	crl::time _blobsLastTime = 0;
	crl::time _blobsHideLastTime = 0;

	Animations::Basic _animation;

};

BlobsWidget::BlobsWidget(
	not_null<RpWidget*> parent,
	rpl::producer<bool> &&hideBlobs)
: RpWidget(parent)
, _blobs(MuteBlobs(), kLevelDuration, kMaxLevel)
, _blobBrush(Qt::transparent)
, _glowBrush(Qt::transparent)
, _blobsLastTime(crl::now()) {
	init();

	for (auto i = 0; i < _blobs.size(); i++) {
		const auto radiuses = _blobs.radiusesAt(i);
		auto radiusesChange = rpl::duplicate(
			hideBlobs
		) | rpl::distinct_until_changed(
		) | rpl::map([=](bool hide) -> Radiuses {
			return hide
				? Radiuses{ radiuses.min, radiuses.min }
				: radiuses;
		});
		_blobs.setRadiusesAt(std::move(radiusesChange), i);
	}

	std::move(
		hideBlobs
	) | rpl::start_with_next([=](bool hide) {
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

		const auto w = _blobs.maxRadius() * 2;
		const auto margins = style::margins(w, w, w, w);
		_inner = QRect(QPoint(), size).marginsRemoved(margins);
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
		_blobs.paint(p, _blobBrush);
	}, lifetime());

	_animation.init([=](crl::time now) {
		if (const auto &last = _blobsHideLastTime; (last > 0)
			&& (now - last >= Paint::Blobs::kHideBlobsDuration)) {
			_animation.stop();
			return false;
		}
		_blobs.updateLevel(now - _blobsLastTime);
		_blobsLastTime = now;

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

QRect BlobsWidget::innerRect() const {
	return _inner;
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
			return IsConnecting(state.type);
		})
	) | rpl::map([](bool animDisabled, bool hide, bool isConnecting) {
		return isConnecting || !(!animDisabled && !hide);
	})))
, _content(base::make_unique_q<AbstractButton>(parent))
, _label(base::make_unique_q<FlatLabel>(
	_content,
	_state.value(
	) | rpl::map([](const CallMuteButtonState &state) {
		return state.text;
	}),
	_st.label))
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
		_content->sizeValue(),
		_label->sizeValue()
	) | rpl::start_with_next([=](QSize my, QSize label) {
		_label->moveToLeft(
			(my.width() - label.width()) / 2,
			my.height() - label.height(),
			my.width());
	}, _label->lifetime());
	_label->setAttribute(Qt::WA_TransparentForMouseEvents);

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
	}, lifetime());

	// State type.
	const auto previousType =
		lifetime().make_state<CallMuteButtonType>(_state.current().type);
	setEnableMouse(false);

	const auto blobsInner = _blobs->innerRect();
	auto linearGradients = anim::linear_gradients<CallMuteButtonType>(
		_colors,
		QPointF(blobsInner.x(), blobsInner.y() + blobsInner.height()),
		QPointF(blobsInner.x() + blobsInner.width(), blobsInner.y()));

	auto glowColors = [&] {
		auto copy = _colors;
		for (auto &[type, colors] : copy) {
			if (IsInactive(type)) {
				colors[0] = st::groupCallBg->c;
			} else {
				colors[0].setAlpha(kGlowAlpha);
			}
			colors[1] = QColor(Qt::transparent);
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

		if (IsInactive(type) && !IsInactive(previous)) {
			setEnableMouse(false);
		}

		const auto crossFrom = IsMuted(previous) ? 0. : 1.;
		const auto crossTo = IsMuted(type) ? 0. : 1.;

		const auto radialShowFrom = IsConnecting(previous) ? 1. : 0.;
		const auto radialShowTo = IsConnecting(type) ? 1. : 0.;

		const auto from = _switchAnimation.animating()
			? (1. - _switchAnimation.value(0.))
			: 0.;
		const auto to = 1.;

		auto callback = [=](float64 value) {
			_blobs->setBlobBrush(QBrush(
				linearGradients.gradient(previous, type, value)));
			_blobs->setGlowBrush(QBrush(
				glows.gradient(previous, type, value)));
			_blobs->update();

			const auto crossProgress = (crossFrom == crossTo)
				? crossTo
				: anim::interpolateF(crossFrom, crossTo, value);
			if (crossProgress != _crossLineProgress) {
				_crossLineProgress = crossProgress;
				_content->update(_muteIconPosition);
			}

			const auto radialShowProgress = (radialShowFrom == radialShowTo)
				? radialShowTo
				: anim::interpolateF(radialShowFrom, radialShowTo, value);
			if (radialShowProgress != _radialShowProgress.current()) {
				_radialShowProgress = radialShowProgress;
			}

			overridesColors(previous, type, value);

			if (value == to) {
				if (!IsInactive(type) && IsInactive(previous)) {
					setEnableMouse(true);
				}
			}
		};

		_switchAnimation.stop();
		const auto duration = (1. - from) * kSwitchStateDuration;
		_switchAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	// Icon rect.
	_content->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto &icon = _st.button.icon;
		const auto &pos = _st.button.iconPosition;

		_muteIconPosition = QRect(
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
			_muteIconPosition.topLeft(),
			1. - _crossLineProgress);

		if (_radial) {
			p.setOpacity(_radialShowProgress.current());
			_radial->draw(
				p,
				_st.bgPosition,
				_content->width());
		}
	}, _content->lifetime());
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

void CallMuteButton::setEnableMouse(bool value) {
	_content->setAttribute(Qt::WA_TransparentForMouseEvents, !value);
}

void CallMuteButton::overridesColors(
		CallMuteButtonType fromType,
		CallMuteButtonType toType,
		float64 progress) {
	const auto toInactive = IsInactive(toType);
	const auto fromInactive = IsInactive(fromType);
	if (toInactive && (progress == 1)) {
		_colorOverrides.fire({ std::nullopt, std::nullopt });
		return;
	}
	auto from = _colors.find(fromType)->second[0];
	auto to = _colors.find(toType)->second[0];
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
