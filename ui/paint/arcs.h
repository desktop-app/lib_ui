// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "styles/style_widgets.h"

class Painter;

namespace Ui::Paint {

class ArcsAnimation {
public:

	enum class Direction {
		Up,
		Down,
		Left,
		Right,
	};

	ArcsAnimation(
		const style::ArcsAnimation &st,
		std::vector<float> thresholds,
		float64 startValue,
		Direction direction);

	void paint(
		Painter &p,
		std::optional<QColor> colorOverride = std::nullopt);

	void setValue(float64 value);

	rpl::producer<> startUpdateRequests();
	rpl::producer<> stopUpdateRequests();

	void update(crl::time now);

	bool isFinished() const;

	float width() const;
	float maxWidth() const;
	float finishedWidth() const;
	float height() const;

	void setStrokeRatio(float ratio);

private:
	struct Arc {
		QRectF rect;
		float threshold;
		crl::time startTime = 0;
		float64 progress = 0.;
	};

	void initArcs(std::vector<float> thresholds);
	QRectF computeArcRect(int index) const;
	bool isHorizontal() const;

	bool isArcFinished(const Arc &arc) const;
	void updateArcStartTime(
		Arc &arc,
		float64 previousValue,
		crl::time now);

	const style::ArcsAnimation &_st;
	const Direction _direction;
	const int _startAngle;
	const int _spanAngle;
	const QRectF _emptyRect;

	float64 _currentValue = 0.;
	float _strokeRatio = 0.;

	rpl::event_stream<> _startUpdateRequests;
	rpl::event_stream<> _stopUpdateRequests;

	std::vector<Arc> _arcs;

};

} // namespace Ui::Paint
