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
	};

	Blobs(
		std::vector<BlobData> blobDatas,
		float levelDuration,
		float maxLevel);

	void setLevel(float value);
	void paint(Painter &p, const QBrush &brush);

	[[nodiscard]] float maxRadius() const;

private:
	void init();

	const float _maxLevel;

	std::vector<BlobData> _blobDatas;
	std::vector<BlobBezier> _blobs;

	crl::time _lastUpdateTime = 0;
	anim::continuous_value _levelValue;

};

} // namespace Ui::Paint
