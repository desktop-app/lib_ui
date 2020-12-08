// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animation_value.h"
#include "ui/paint/blob_linear.h"

class Painter;

namespace Ui::Paint {

class LinearBlobs final {
public:
	struct BlobData {
		int segmentsCount = 0;
		float minScale = 0;
		float minRadius = 0;
		float maxRadius = 0;
		float speedScale = 0;
		float alpha = 0;
		int topOffset = 0;
	};

	LinearBlobs(
		std::vector<BlobData> blobDatas,
		float levelDuration,
		float levelDuration2,
		float maxLevel);

	void setRadiusesAt(
		rpl::producer<LinearBlobBezier::Radiuses> &&radiuses,
		int index);
	LinearBlobBezier::Radiuses radiusesAt(int index);

	void setLevel(float value);
	void paint(
		Painter &p,
		const QBrush &brush,
		const QRect &rect,
		float pinnedTop,
		float progressToPinned);
	void updateLevel(crl::time dt);

	[[nodiscard]] float maxRadius() const;
	[[nodiscard]] int size() const;
	[[nodiscard]] float64 currentLevel() const;

	static constexpr auto kHideBlobsDuration = 2000;

private:
	void init();

	const float _maxLevel;

	std::vector<BlobData> _blobDatas;
	std::vector<LinearBlobBezier> _blobs;

	anim::continuous_value _levelValue;
	anim::continuous_value _levelValue2;

	rpl::lifetime _lifetime;

};

} // namespace Ui::Paint
