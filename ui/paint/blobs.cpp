// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/paint/blobs.h"

#include "ui/painter.h"

namespace Ui::Paint {

namespace {

constexpr auto kRateLimitF = 1000. / 60.;
constexpr auto kRateLimit = int(kRateLimitF + 0.5); // Round.

} // namespace

Blobs::Blobs(
	std::vector<BlobData> blobDatas,
	float levelDuration,
	float maxLevel)
: _maxLevel(maxLevel)
, _blobDatas(std::move(blobDatas))
, _levelValue(levelDuration) {
	init();
}

void Blobs::init() {
	for (const auto &data : _blobDatas) {
		auto blob = Paint::RadialBlob(
			data.segmentsCount,
			data.minScale,
			data.minSpeed,
			data.maxSpeed);
		blob.setRadiuses({ data.minRadius, data.maxRadius });
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

void Blobs::setRadiusesAt(
		rpl::producer<Blob::Radiuses> &&radiuses,
		int index) {
	Expects(index >= 0 && index < size());
	std::move(
		radiuses
	) | rpl::start_with_next([=](Blob::Radiuses r) {
		_blobs[index].setRadiuses(std::move(r));
	}, _lifetime);
}

Blob::Radiuses Blobs::radiusesAt(int index) {
	Expects(index >= 0 && index < size());
	return _blobs[index].radiuses();
}

void Blobs::setLevel(float value) {
	const auto to = std::min(_maxLevel, value) / _maxLevel;
	_levelValue.start(to);
}

void Blobs::resetLevel() {
	_levelValue.reset();
}

void Blobs::paint(Painter &p, const QBrush &brush, float outerScale) {
	const auto opacity = p.opacity();
	for (auto i = 0; i < _blobs.size(); i++) {
		const auto alpha = _blobDatas[i].alpha;
		if (alpha != 1.) {
			p.setOpacity(opacity * alpha);
		}
		_blobs[i].paint(p, brush, outerScale);
		if (alpha != 1.) {
			p.setOpacity(opacity);
		}
	}
}

void Blobs::updateLevel(crl::time dt) {
	const auto limitedDt = (dt > 20) ? kRateLimit : dt;
	_levelValue.update(limitedDt);

	for (auto i = 0; i < _blobs.size(); i++) {
		_blobs[i].update(
			_levelValue.current(),
			_blobDatas[i].speedScale,
			limitedDt / kRateLimitF);
	}
}

float64 Blobs::currentLevel() const {
	return _levelValue.current();
}

} // namespace Ui::Paint
