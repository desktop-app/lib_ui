// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace style {
struct PillTabs;
} // namespace style

namespace st {
extern const style::PillTabs &defaultPillTabs;
} // namespace st

namespace Ui {

class PillTabs final : public RpWidget {
public:
	PillTabs(
		QWidget *parent,
		const std::vector<QString> &labels,
		int activeIndex = 0,
		const style::PillTabs &st = st::defaultPillTabs);

	void setActiveIndex(int index);
	void setOpacity(float64 opacity);
	[[nodiscard]] int activeIndex() const;
	[[nodiscard]] rpl::producer<int> activeIndexChanges() const;

private:
	void setupButtons();
	void paint();

	const style::PillTabs &_st;
	std::vector<QString> _labels;
	int _activeIndex = 0;
	float64 _opacity = 1.;
	Animations::Simple _animation;
	float64 _animatedPosition = 0.;
	rpl::event_stream<int> _activeIndexChanges;

};

} // namespace Ui
