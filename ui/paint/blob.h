// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

class Painter;

namespace Ui::Paint {

class BlobBezier final {
public:
	BlobBezier(int n, float minScale, float minSpeed = 0, float maxSpeed = 0);

	void paint(Painter &p, const QBrush &brush);
	void update(float level, float speedScale);
	void generateBlob();

	void setMinRadius(float value);
	void setMaxRadius(float value);

private:
	struct Segment {
		float radius = 0.;
		float angle = 0.;
		float radiusNext = 0.;
		float angleNext = 0.;
		float progress = 0.;
		float speed = 0.;
	};

	void generateBlob(float &radius, float &angle, int i);

	const int _segmentsCount;
	const float64 _segmentLength;
	const float _minScale;
	const float _minSpeed;
	const float _maxSpeed;
	const QPen _pen;
	const float _segmentAngle;

	std::vector<Segment> _segments;

	float64 _scale = 0;
	float _minRadius = 0.;
	float _maxRadius = 0.;

};

} // namespace Ui::Paint
