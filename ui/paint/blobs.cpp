// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/paint/blobs.h"

#include "ui/painter.h"

namespace Ui::Paint {

Blobs::Blobs(
	std::vector<BlobData> blobDatas,
	float levelDuration,
	float maxLevel)
: _maxLevel(maxLevel)
, _blobDatas(blobDatas)
, _levelValue(levelDuration) {
	init();
}

void Blobs::init() {
	for (const auto &data : _blobDatas) {
		auto blob = Paint::BlobBezier(data.segmentsCount, data.minScale);
		blob.setRadius(data.minRadius, data.maxRadius);
		_blobs.push_back(std::move(blob));
	}
}

float Blobs::maxRadius() const {
	const auto maxOfRadiuses = [](const BlobData data) {
		return std::max(data.maxRadius, data.minRadius);
	};
	const auto max = *ranges::max_element(
		_blobDatas,
		std::less<>(),
		maxOfRadiuses);
	return maxOfRadiuses(max);
}

void Blobs::setLevel(float value) {
	const auto to = std::min(_maxLevel, value) / _maxLevel;
	_levelValue.start(to);
}

void Blobs::paint(Painter &p, const QBrush &brush) {
	const auto dt = crl::now() - _lastUpdateTime;
	_levelValue.update((dt > 20) ? 17 : dt);

	const auto opacity = p.opacity();
	for (auto i = 0; i < _blobs.size(); i++) {
		_blobs[i].update(_levelValue.current(), _blobDatas[i].speedScale);
		const auto alpha = _blobDatas[i].alpha;
		if (alpha != 1.) {
			p.setOpacity(opacity * alpha);
		}
		_blobs[i].paint(p, brush);
	}
	_lastUpdateTime = crl::now();
}

} // namespace Ui::Paint
