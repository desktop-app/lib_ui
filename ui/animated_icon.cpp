// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/animated_icon.h"

#include "ui/image/image_prepare.h"
#include "ui/style/style_core.h"
#include "ui/effects/frame_generator.h"

#include <QtGui/QPainter>
#include <crl/crl_async.h>
#include <crl/crl_semaphore.h>
#include <crl/crl_on_main.h>

namespace Ui {
namespace {

constexpr auto kDefaultDuration = crl::time(800);

} // namespace

struct AnimatedIcon::Frame {
	FrameGenerator::Frame generated;
	QImage resizedImage;
	int index = 0;
};

class AnimatedIcon::Impl final : public std::enable_shared_from_this<Impl> {
public:
	explicit Impl(base::weak_ptr<AnimatedIcon> weak);

	void prepareFromAsync(
		FnMut<std::unique_ptr<FrameGenerator>()> factory,
		QSize sizeOverride);
	void waitTillPrepared() const;

	[[nodiscard]] bool valid() const;
	[[nodiscard]] QSize size() const;
	[[nodiscard]] int framesCount() const;
	[[nodiscard]] double frameRate() const;
	[[nodiscard]] Frame &frame();
	[[nodiscard]] const Frame &frame() const;

	[[nodiscard]] crl::time animationDuration() const;
	void moveToFrame(int frame, QSize updatedDesiredSize);

private:
	enum class PreloadState {
		None,
		Preloading,
		Ready,
	};

	// Called from crl::async.
	void renderPreloadFrame();

	std::unique_ptr<FrameGenerator> _generator;
	Frame _current;
	QSize _desiredSize;
	std::atomic<PreloadState> _preloadState = PreloadState::None;

	Frame _preloaded; // Changed on main or async depending on _preloadState.
	QSize _preloadImageSize;

	base::weak_ptr<AnimatedIcon> _weak;
	int _framesCount = 0;
	double _frameRate = 0.;
	mutable crl::semaphore _semaphore;
	mutable bool _ready = false;

};

AnimatedIcon::Impl::Impl(base::weak_ptr<AnimatedIcon> weak)
: _weak(weak) {
}

void AnimatedIcon::Impl::prepareFromAsync(
		FnMut<std::unique_ptr<FrameGenerator>()> factory,
		QSize sizeOverride) {
	const auto guard = gsl::finally([&] { _semaphore.release(); });
	if (!_weak) {
		return;
	}
	auto generator = factory ? factory() : nullptr;
	if (!generator || !_weak) {
		return;
	}
	_framesCount = generator->count();
	_frameRate = generator->rate();
	_current.generated = generator->renderNext(QImage(), sizeOverride);
	if (_current.generated.image.isNull()) {
		return;
	}
	_generator = std::move(generator);
	_desiredSize = sizeOverride.isEmpty()
		? style::ConvertScale(_current.generated.image.size())
		: sizeOverride;
}

void AnimatedIcon::Impl::waitTillPrepared() const {
	if (!_ready) {
		_semaphore.acquire();
		_ready = true;
	}
}

bool AnimatedIcon::Impl::valid() const {
	waitTillPrepared();
	return (_generator != nullptr);
}

QSize AnimatedIcon::Impl::size() const {
	waitTillPrepared();
	return _desiredSize;
}

int AnimatedIcon::Impl::framesCount() const {
	waitTillPrepared();
	return _framesCount;
}

double AnimatedIcon::Impl::frameRate() const {
	waitTillPrepared();
	return _frameRate;
}

AnimatedIcon::Frame &AnimatedIcon::Impl::frame() {
	waitTillPrepared();
	return _current;
}

const AnimatedIcon::Frame &AnimatedIcon::Impl::frame() const {
	waitTillPrepared();
	return _current;
}

crl::time AnimatedIcon::Impl::animationDuration() const {
	waitTillPrepared();
	const auto rate = _generator ? _generator->rate() : 0.;
	const auto frames = _generator ? _generator->count() : 0;
	return (frames && rate >= 1.)
		? crl::time(base::SafeRound(frames / rate * 1000.))
		: 0;
}

void AnimatedIcon::Impl::moveToFrame(int frame, QSize updatedDesiredSize) {
	waitTillPrepared();
	const auto state = _preloadState.load();
	const auto shown = _current.index;
	if (!updatedDesiredSize.isEmpty()) {
		_desiredSize = updatedDesiredSize;
	}
	const auto desiredImageSize = _desiredSize * style::DevicePixelRatio();
	if (!_generator
		|| state == PreloadState::Preloading
		|| (shown == frame
			&& (_current.generated.image.size() == desiredImageSize))) {
		return;
	} else if (state == PreloadState::Ready) {
		if (_preloaded.index == frame
			&& (shown != frame
				|| _preloaded.generated.image.size() == desiredImageSize)) {
			std::swap(_current, _preloaded);
			if (_current.generated.image.size() == desiredImageSize) {
				return;
			}
		} else if ((shown < _preloaded.index && _preloaded.index < frame)
			|| (shown > _preloaded.index && _preloaded.index > frame)) {
			std::swap(_current, _preloaded);
		}
	}
	_preloadImageSize = desiredImageSize;
	_preloaded.index = frame;
	_preloadState = PreloadState::Preloading;
	crl::async([guard = shared_from_this()] {
		guard->renderPreloadFrame();
	});
}

void AnimatedIcon::Impl::renderPreloadFrame() {
	if (!_weak) {
		return;
	}
	if (_preloaded.index == 0) {
		_generator->jumpToStart();
	}
	_preloaded.generated = (_preloaded.index && _preloaded.index == _current.index)
		? _generator->renderCurrent(
			std::move(_preloaded.generated.image),
			_preloadImageSize)
		: _generator->renderNext(
			std::move(_preloaded.generated.image),
			_preloadImageSize);
	_preloaded.resizedImage = QImage();
	_preloadState = PreloadState::Ready;
	crl::on_main(_weak, [=] {
		_weak->frameJumpFinished();
	});
}

AnimatedIcon::AnimatedIcon(AnimatedIconDescriptor &&descriptor)
: _impl(std::make_shared<Impl>(base::make_weak(this)))
, _colorized(descriptor.colorized) {
	crl::async([
		impl = _impl,
		factory = std::move(descriptor.generator),
		sizeOverride = descriptor.sizeOverride
	]() mutable {
		impl->prepareFromAsync(std::move(factory), sizeOverride);
	});
}

void AnimatedIcon::wait() const {
	_impl->waitTillPrepared();
}

bool AnimatedIcon::valid() const {
	return _impl->valid();
}

int AnimatedIcon::frameIndex() const {
	return _impl->frame().index;
}

int AnimatedIcon::framesCount() const {
	return _impl->framesCount();
}

double AnimatedIcon::frameRate() const {
	return _impl->frameRate();
}

QImage AnimatedIcon::frame(const QColor &textColor) const {
	return frame(textColor, QSize(), nullptr).image;
}

QImage AnimatedIcon::notColorizedFrame() const {
	return notColorizedFrame(QSize(), nullptr).image;
}

AnimatedIcon::ResizedFrame AnimatedIcon::frame(
		const QColor &textColor,
		QSize desiredSize,
		Fn<void()> updateWithPerfect) const {
	auto result = notColorizedFrame(
		desiredSize,
		std::move(updateWithPerfect));
	if (_colorized) {
		auto &image = result.image;
		style::colorizeImage(image, textColor, &image, {}, {}, true);
	}
	return result;
}

AnimatedIcon::ResizedFrame AnimatedIcon::notColorizedFrame(
		QSize desiredSize,
		Fn<void()> updateWithPerfect) const {
	auto &frame = _impl->frame();
	preloadNextFrame(crl::now(), &frame, desiredSize);
	const auto desired = size() * style::DevicePixelRatio();
	if (frame.generated.image.isNull()) {
		return { frame.generated.image };
	} else if (frame.generated.image.size() == desired) {
		return { frame.generated.image };
	} else if (frame.resizedImage.size() != desired) {
		frame.resizedImage = frame.generated.image.scaled(
			desired,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}
	if (updateWithPerfect) {
		_repaint = std::move(updateWithPerfect);
	}
	return { frame.resizedImage, true };
}

int AnimatedIcon::width() const {
	return size().width();
}

int AnimatedIcon::height() const {
	return size().height();
}

QSize AnimatedIcon::size() const {
	return _impl->size();
}

void AnimatedIcon::paint(QPainter &p, int x, int y) {
	auto &frame = _impl->frame();
	preloadNextFrame(crl::now(), &frame);
	if (frame.generated.image.isNull()) {
		return;
	}
	const auto rect = QRect{ QPoint(x, y), size() };
	p.drawImage(rect, frame.generated.image);
}

void AnimatedIcon::paintInCenter(QPainter &p, QRect rect) {
	const auto my = size();
	paint(
		p,
		rect.x() + (rect.width() - my.width()) / 2,
		rect.y() + (rect.height() - my.height()) / 2);
}

void AnimatedIcon::animate(Fn<void()> update) {
	if (framesCount() != 1 && !anim::Disabled()) {
		jumpToStart(std::move(update));
		_animationDuration = _impl->animationDuration();
		_animationCurrentStart = _animationStarted = crl::now();
		continueAnimation(_animationCurrentStart);
	}
}

void AnimatedIcon::continueAnimation(crl::time now) {
	const auto callback = [=](float64 value) {
		if (anim::Disabled()) {
			return;
		}
		const auto elapsed = int(value);
		const auto now = _animationStartTime + elapsed;
		if (!_animationDuration && elapsed > kDefaultDuration / 2) {
			auto animation = std::move(_animation);
			continueAnimation(now);
		}
		preloadNextFrame(now);
		if (_repaint) _repaint();
	};
	const auto duration = _animationDuration
		? _animationDuration
		: kDefaultDuration;
	_animationStartTime = now;
	_animation.start(callback, 0., 1. * duration, duration);
}

void AnimatedIcon::jumpToStart(Fn<void()> update) {
	_repaint = std::move(update);
	_animation.stop();
	_animationCurrentIndex = 0;
	_impl->moveToFrame(0, QSize());
}

void AnimatedIcon::frameJumpFinished() {
	if (_repaint && !animating()) {
		_repaint();
		_repaint = nullptr;
	}
}

int AnimatedIcon::wantedFrameIndex(
		crl::time now,
		const Frame *resolvedCurrent) const {
	const auto frame = resolvedCurrent ? resolvedCurrent : &_impl->frame();
	if (frame->index == _animationCurrentIndex + 1) {
		++_animationCurrentIndex;
		_animationCurrentStart = _animationNextStart;
	}
	if (!_animation.animating()) {
		return _animationCurrentIndex;
	}
	if (frame->index == _animationCurrentIndex) {
		const auto duration = frame->generated.duration;
		const auto next = _animationCurrentStart + duration;
		if (frame->generated.last) {
			_animation.stop();
			if (_repaint) _repaint();
			return _animationCurrentIndex;
		} else if (now < next) {
			return _animationCurrentIndex;
		}
		_animationNextStart = next;
		return _animationCurrentIndex + 1;
	}
	Assert(!_animationCurrentIndex);
	return 0;
}

void AnimatedIcon::preloadNextFrame(
		crl::time now,
		const Frame *resolvedCurrent,
		QSize updatedDesiredSize) const {
	_impl->moveToFrame(
		wantedFrameIndex(now, resolvedCurrent),
		updatedDesiredSize);
}

bool AnimatedIcon::animating() const {
	return _animation.animating();
}

std::unique_ptr<AnimatedIcon> MakeAnimatedIcon(
		AnimatedIconDescriptor &&descriptor) {
	return std::make_unique<AnimatedIcon>(std::move(descriptor));
}

} // namespace Lottie
