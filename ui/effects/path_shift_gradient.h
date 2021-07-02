// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core_types.h"

#include <QtGui/QLinearGradient>

namespace Ui {

class PathShiftGradient final {
public:
	PathShiftGradient(
		const style::color &bg,
		const style::color &fg,
		Fn<void()> animationCallback);
	~PathShiftGradient();

	void startFrame(
		int viewportLeft,
		int viewportWidth,
		int gradientWidth);

	void overrideColors(const style::color &bg, const style::color &fg);
	void clearOverridenColors();

	using Background = std::variant<QLinearGradient*, style::color>;
	bool paint(Fn<bool(const Background&)> painter);

private:
	struct AnimationData;

	void refreshColors();
	void refreshColors(const style::color &bg, const style::color &fg);
	void updateGeometry();
	void activateAnimation();

	static std::weak_ptr<AnimationData> Animation;

	const style::color &_bg;
	const style::color &_fg;
	const style::color *_bgOverride = nullptr;
	QLinearGradient _gradient;
	std::shared_ptr<AnimationData> _animation;
	const Fn<void()> _animationCallback;
	int _viewportLeft = 0;
	int _viewportWidth = 0;
	int _gradientWidth = 0;
	int _gradientStart = 0;
	int _gradientFinalStop = 0;
	bool _gradientEnabled = false;
	bool _geometryUpdated = false;
	bool _animationActive = false;
	bool _colorsOverriden = false;

	rpl::lifetime _lifetime;

};

} // namespace Ui
