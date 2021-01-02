// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

class Painter;

namespace Ui::Paint {

class Blob {
public:
	struct Radiuses {
		float min = 0.;
		float max = 0.;
	};

	Blob(int n, float minSpeed = 0, float maxSpeed = 0);
	virtual ~Blob() = default;

	void update(float level, float speedScale, float64 rate);
	void generateBlob();

	void setRadiuses(Radiuses values);
	[[nodiscard]] Radiuses radiuses() const;

protected:
	struct TwoValues {
		float current = 0.;
		float next = 0.;
		void setNext(float v) {
			current = next;
			next = v;
		}
	};

	struct Segment {
		float progress = 0.;
		float speed = 0.;
	};

	void generateSingleValues(int i);
	virtual void generateTwoValues(int i) = 0;
	virtual Segment &segmentAt(int i) = 0;

	const int _segmentsCount;
	const float _minSpeed;
	const float _maxSpeed;
	const QPen _pen;

	Radiuses _radiuses;

};

class RadialBlob final : public Blob {
public:
	RadialBlob(int n, float minScale, float minSpeed = 0, float maxSpeed = 0);

	void paint(Painter &p, const QBrush &brush, float outerScale = 1.);
	void update(float level, float speedScale, float64 rate);

private:
	struct Segment : Blob::Segment {
		Blob::TwoValues radius;
		Blob::TwoValues angle;
	};

	void generateTwoValues(int i) override;
	Blob::Segment &segmentAt(int i) override;

	const float64 _segmentLength;
	const float _minScale;
	const float _segmentAngle;
	const float _angleDiff;

	std::vector<Segment> _segments;

	float64 _scale = 0;

};

class LinearBlob final : public Blob {
public:
	enum class Direction {
		TopDown,
		BottomUp,
	};

	LinearBlob(
		int n,
		Direction direction = Direction::TopDown,
		float minSpeed = 0,
		float maxSpeed = 0);

	void paint(Painter &p, const QBrush &brush, int width);

private:
	struct Segment : Blob::Segment {
		Blob::TwoValues radius;
	};

	void generateTwoValues(int i) override;
	Blob::Segment &segmentAt(int i) override;

	const int _topDown;

	std::vector<Segment> _segments;

};

} // namespace Ui::Paint
