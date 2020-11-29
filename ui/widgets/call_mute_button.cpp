// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/call_mute_button.h"

#include "base/event_filter.h"
#include "ui/paint/blobs.h"
#include "ui/painter.h"

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Ui {

namespace {

constexpr auto kMaxLevel = 1.;

constexpr auto kLevelDuration = 100. + 500. * 0.33;

constexpr auto kScaleBig = 0.807 - 0.1;
constexpr auto kScaleSmall = 0.704 - 0.1;

constexpr auto kScaleBigMin = 0.878;
constexpr auto kScaleSmallMin = 0.926;

constexpr auto kScaleBigMax = kScaleBigMin + kScaleBig;
constexpr auto kScaleSmallMax = kScaleSmallMin + kScaleSmall;

constexpr auto kMainRadiusFactor = 50. / 57.;

constexpr auto kSwitchStateDuration = 120;

constexpr auto MuteBlobs() -> std::array<Paint::Blobs::BlobData, 3> {
	return {{
		{
			.segmentsCount = 6,
			.minScale = 1.,
			.minRadius = 57. * kMainRadiusFactor,
			.maxRadius = 63. * kMainRadiusFactor,
			.speedScale = .4,
			.alpha = 1.,
		},
		{
			.segmentsCount = 9,
			.minScale = kScaleSmallMin / kScaleSmallMax,
			.minRadius = 62 * kScaleSmallMax * kMainRadiusFactor,
			.maxRadius = 72 * kScaleSmallMax * kMainRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
		{
			.segmentsCount = 12,
			.minScale = kScaleBigMin / kScaleBigMax,
			.minRadius = 65 * kScaleBigMax * kMainRadiusFactor,
			.maxRadius = 75 * kScaleBigMax * kMainRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
	}};
}

} // namespace

class BlobsWidget final : public RpWidget {
public:
	BlobsWidget(not_null<RpWidget*> parent);

	void setLevel(float level);
	void requestPaintProgress(float64 progress);

private:
	void init();

	Paint::Blobs _blobs;

	int _center = 0;
	float64 _progress = 0;

	Animations::Basic _animation;

};

BlobsWidget::BlobsWidget(not_null<RpWidget*> parent)
: RpWidget(parent)
, _blobs(MuteBlobs() | ranges::to_vector, kLevelDuration, kMaxLevel) {
	init();
}

void BlobsWidget::init() {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto maxBlobDiameter = _blobs.maxRadius() * 2;
	resize(maxBlobDiameter, maxBlobDiameter);

	const auto gradient = anim::linear_gradient(
		{ st::groupCallMuted1->c, st::groupCallMuted2->c },
		{ st::groupCallLive1->c, st::groupCallLive2->c },
		QPoint(0, height()),
		QPoint(width(), 0));

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_center = size.width() / 2;
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		PainterHighQualityEnabler hq(p);
		p.translate(_center, _center);
		_blobs.paint(p, QBrush(gradient.gradient(_progress)));
	}, lifetime());

	_animation.init([=](crl::time now) {
		const auto dt = now - _animation.started();
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

void BlobsWidget::setLevel(float level) {
	_blobs.setLevel(level);
}

void BlobsWidget::requestPaintProgress(float64 progress) {
	if (progress == _progress) {
		return;
	}
	_progress = progress;
	update();
}

CallMuteButton::CallMuteButton(
	not_null<RpWidget*> parent,
	CallMuteButtonState initial)
: _state(initial)
, _blobs(base::make_unique_q<BlobsWidget>(parent))
, _content(parent, st::callMuteButtonActive, &st::callMuteButtonMuted)
, _connecting(parent, st::callMuteButtonConnecting)
, _crossLineMuteAnimation(st::callMuteCrossLine) {
	init();
}

void CallMuteButton::init() {
	if (_state.type == CallMuteButtonType::Connecting
		|| _state.type == CallMuteButtonType::ForceMuted) {
		_connecting.setText(rpl::single(_state.text));
		_connecting.show();
		_content.hide();
	} else {
		_content.setText(rpl::single(_state.text));
		_content.setProgress((_state.type == CallMuteButtonType::Muted) ? 1. : 0.);
		_connecting.hide();
		_content.show();
	}
	_connecting.setAttribute(Qt::WA_TransparentForMouseEvents);

	_content.sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto &icon = st::callMuteButtonActive.button.icon;
		const auto &pos = st::callMuteButtonActive.button.iconPosition;

		_muteIconPosition = QPoint(
			(pos.x() < 0) ? ((size.width() - icon.width()) / 2) : pos.x(),
			(pos.y() < 0) ? ((size.height() - icon.height()) / 2) : pos.y());
	}, lifetime());

	auto filterCallback = [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::Paint) {
			return base::EventFilterResult::Continue;
		}
		contentPaint();
		return base::EventFilterResult::Cancel;
	};

	auto filter = base::install_event_filter(
		&_content,
		std::move(filterCallback));

	lifetime().make_state<base::unique_qptr<QObject>>(std::move(filter));
}

void CallMuteButton::contentPaint() {
	Painter p(&_content);

	const auto progress = 1. - _crossLineProgress;
	_crossLineMuteAnimation.paint(p, _muteIconPosition, progress);
}

void CallMuteButton::setState(const CallMuteButtonState &state) {
	if (state.type == CallMuteButtonType::Connecting
		|| state.type == CallMuteButtonType::ForceMuted) {
		if (_state.text != state.text) {
			_connecting.setText(rpl::single(state.text));
		}
		if (!_connecting.isHidden() || !_content.isHidden()) {
			_connecting.show();
		}
		_content.setOuterValue(0.);
		_content.hide();
	} else {
		if (_state.text != state.text) {
			_content.setText(rpl::single(state.text));
		}

		const auto isWasActive = (_state.type == CallMuteButtonType::Active);
		const auto isActive = (state.type == CallMuteButtonType::Active);
		{
			const auto from = _switchAnimation.value(isWasActive ? 1. : 0.);
			const auto to = isActive ? 1. : 0.;
			_switchAnimation.start(
				[=](auto value) {
					_blobs->requestPaintProgress(value);
					_crossLineProgress = value;
					_content.update();
				},
				from,
				to,
				kSwitchStateDuration);
		}

		_content.setProgress((state.type == CallMuteButtonType::Muted) ? 1. : 0.);
		if (!_connecting.isHidden() || !_content.isHidden()) {
			_content.show();
		}
		_connecting.hide();
		if (isActive) {
			_content.setOuterValue(_level);
		} else {
			_content.setOuterValue(0.);
		}
	}
	_state = state;
}

void CallMuteButton::setLevel(float level) {
	_level = level;
	_blobs->setLevel(level);
	if (_state.type == CallMuteButtonType::Active) {
		_content.setOuterValue(level);
	}
}

rpl::producer<Qt::MouseButton> CallMuteButton::clicks() const {
	return _content.clicks();
}

QSize CallMuteButton::innerSize() const {
	return innerGeometry().size();
}

QRect CallMuteButton::innerGeometry() const {
	const auto skip = st::callMuteButtonActive.outerRadius;
	return QRect(
		_content.x(),
		_content.y(),
		_content.width() - 2 * skip,
		_content.width() - 2 * skip);
}

void CallMuteButton::moveInner(QPoint position) {
	const auto skip = st::callMuteButtonActive.outerRadius;
	_content.move(position - QPoint(skip, skip));
	_connecting.move(_content.pos());

	{
		const auto offset = QPoint(
			(_blobs->width() - _content.width()) / 2,
			(_blobs->height() - _content.width()) / 2);
		_blobs->move(_content.pos() - offset);
	}
}

void CallMuteButton::setVisible(bool visible) {
	if (!visible) {
		_content.hide();
		_connecting.hide();
	} else if (_state.type == CallMuteButtonType::Connecting
		|| _state.type == CallMuteButtonType::ForceMuted) {
		_connecting.show();
	} else {
		_content.show();
	}
}

void CallMuteButton::raise() {
	_blobs->raise();
	_content.raise();
	_connecting.raise();
}

void CallMuteButton::lower() {
	_content.lower();
	_connecting.lower();
	_blobs->lower();
}

rpl::lifetime &CallMuteButton::lifetime() {
	return _content.lifetime();
}

} // namespace Ui
