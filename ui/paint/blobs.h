// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animation_value.h"
#include "ui/paint/blob.h"

class Painter;

namespace Ui::Paint {

class Blobs final {
public:
	struct BlobData {
		int segmentsCount = 0;
		float minScale = 0;
		float minRadius = 0;
		float maxRadius = 0;
		float speedScale = 0;
		float alpha = 0;
		float minSpeed = 0;
		float maxSpeed = 0;
	};

	Blobs(
		std::vector<BlobData> blobDatas,
		float levelDuration,
		float maxLevel);

	void setRadiusesAt(
		rpl::producer<Blob::Radiuses> &&radiuses,
		int index);
	Blob::Radiuses radiusesAt(int index);

	void setLevel(float value);
	void resetLevel();
	void paint(QPainter &p, const QBrush &brush, float outerScale = 1.);
	void updateLevel(crl::time dt);

	[[nodiscard]] float maxRadius() const;
	[[nodiscard]] int size() const;
	[[nodiscard]] float64 currentLevel() const;

	static constexpr auto kHideBlobsDuration = 2000;

private:
	void init();

	const float _maxLevel;

	std::vector<BlobData> _blobDatas;
	std::vector<RadialBlob> _blobs;

	anim::continuous_value _levelValue;

	rpl::lifetime _lifetime;

};

} // namespace Ui::Paint
