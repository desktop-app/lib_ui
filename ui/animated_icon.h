// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core_types.h"
#include "ui/effects/animations.h"
#include "base/weak_ptr.h"

#include <crl/crl_time.h>
#include <QtCore/QByteArray>
#include <optional>

namespace Ui {

class FrameGenerator;

struct AnimatedIconDescriptor {
	FnMut<std::unique_ptr<FrameGenerator>()> generator;
	QSize sizeOverride;
	bool colorized = false;
};

class AnimatedIcon final : public base::has_weak_ptr {
public:
	explicit AnimatedIcon(AnimatedIconDescriptor &&descriptor);
	AnimatedIcon(const AnimatedIcon &other) = delete;
	AnimatedIcon &operator=(const AnimatedIcon &other) = delete;
	AnimatedIcon(AnimatedIcon &&other) = delete; // _animation captures this.
	AnimatedIcon &operator=(AnimatedIcon &&other) = delete;

	[[nodiscard]] bool valid() const;
	[[nodiscard]] int frameIndex() const;
	[[nodiscard]] int framesCount() const;
	[[nodiscard]] double frameRate() const;
	[[nodiscard]] QImage frame(const QColor &textColor) const;
	[[nodiscard]] QImage notColorizedFrame() const;
	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] QSize size() const;

	struct ResizedFrame {
		QImage image;
		bool scaled = false;
	};
	[[nodiscard]] ResizedFrame frame(
		const QColor &textColor,
		QSize desiredSize,
		Fn<void()> updateWithPerfect) const;
	[[nodiscard]] ResizedFrame notColorizedFrame(
		QSize desiredSize,
		Fn<void()> updateWithPerfect) const;

	void animate(Fn<void()> update);
	void jumpToStart(Fn<void()> update);

	void paint(QPainter &p, int x, int y);
	void paintInCenter(QPainter &p, QRect rect);

	[[nodiscard]] bool animating() const;

private:
	struct Frame;
	class Impl;
	friend class Impl;

	void wait() const;
	[[nodiscard]] int wantedFrameIndex(
		crl::time now,
		const Frame *resolvedCurrent = nullptr) const;
	void preloadNextFrame(
		crl::time now,
		const Frame *resolvedCurrent = nullptr,
		QSize updatedDesiredSize = QSize()) const;
	void frameJumpFinished();
	void continueAnimation(crl::time now);

	std::shared_ptr<Impl> _impl;
	crl::time _animationStartTime = 0;
	crl::time _animationStarted = 0;
	mutable Animations::Simple _animation;
	mutable Fn<void()> _repaint;
	mutable crl::time _animationDuration = 0;
	mutable crl::time _animationCurrentStart = 0;
	mutable crl::time _animationNextStart = 0;
	mutable int _animationCurrentIndex = 0;
	bool _colorized = false;

};

[[nodiscard]] std::unique_ptr<AnimatedIcon> MakeAnimatedIcon(
	AnimatedIconDescriptor &&descriptor);

} // namespace Ui
