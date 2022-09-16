// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animation_value.h"
#include "ui/paint/blob.h"

namespace Ui::Paint {

class LinearBlobs final {
public:
	struct BlobData {
		int segmentsCount = 0;
		float minRadius = 0;
		float maxRadius = 0;
		float idleRadius = 0;
		float speedScale = 0;
		float alpha = 0;
		float minSpeed = 0;
		float maxSpeed = 0;
	};

	LinearBlobs(
		std::vector<BlobData> blobDatas,
		float levelDuration,
		float maxLevel,
		LinearBlob::Direction direction);

	void setRadiusesAt(
		rpl::producer<Blob::Radiuses> &&radiuses,
		int index);
	Blob::Radiuses radiusesAt(int index);

	void setLevel(float value);
	void paint(QPainter &p, const QBrush &brush, int width);
	void updateLevel(crl::time dt);

	[[nodiscard]] float maxRadius() const;
	[[nodiscard]] int size() const;
	[[nodiscard]] float64 currentLevel() const;

	static constexpr auto kHideBlobsDuration = 2000;

private:
	void init();

	const float _maxLevel;
	const LinearBlob::Direction _direction;

	std::vector<BlobData> _blobDatas;
	std::vector<LinearBlob> _blobs;

	anim::continuous_value _levelValue;

	rpl::lifetime _lifetime;

};

} // namespace Ui::Paint
