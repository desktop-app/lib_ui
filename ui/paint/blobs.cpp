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
		blob.setMinRadius(data.minRadius);
		blob.setMaxRadius(data.maxRadius);
		blob.generateBlob();
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

int Blobs::size() const {
	return _blobs.size();
}

void Blobs::setRadiusAt(
		rpl::producer<float> &&radius,
		int index,
		bool isMax) {
	Expects(index >= 0 && index < size());
	std::move(
		radius
	) | rpl::start_with_next([=](float r) {
		if (isMax) {
			_blobs[index].setMaxRadius(r);
		} else {
			_blobs[index].setMinRadius(r);
		}
	}, _lifetime);
}

void Blobs::setLevel(float value) {
	const auto to = std::min(_maxLevel, value) / _maxLevel;
	_levelValue.start(to);
}

void Blobs::paint(Painter &p, const QBrush &brush) {
	const auto opacity = p.opacity();
	for (auto i = 0; i < _blobs.size(); i++) {
		_blobs[i].update(_levelValue.current(), _blobDatas[i].speedScale);
		const auto alpha = _blobDatas[i].alpha;
		if (alpha != 1.) {
			p.setOpacity(opacity * alpha);
		}
		_blobs[i].paint(p, brush);
	}
}

void Blobs::updateLevel(crl::time dt) {
	_levelValue.update((dt > 20) ? 17 : dt);
}

float64 Blobs::currentLevel() const {
	return _levelValue.current();
}

} // namespace Ui::Paint
