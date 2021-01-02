// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/paint/blobs_linear.h"

#include "ui/painter.h"

namespace Ui::Paint {

namespace {

constexpr auto kRateLimitF = 1000. / 60.;
constexpr auto kRateLimit = int(kRateLimitF + 0.5); // Round.

} // namespace

LinearBlobs::LinearBlobs(
	std::vector<BlobData> blobDatas,
	float levelDuration,
	float maxLevel,
	LinearBlob::Direction direction)
: _maxLevel(maxLevel)
, _direction(direction)
, _blobDatas(std::move(blobDatas))
, _levelValue(levelDuration) {
	init();
}

void LinearBlobs::init() {
	for (const auto &data : _blobDatas) {
		auto blob = Paint::LinearBlob(
			data.segmentsCount,
			_direction,
			data.minSpeed,
			data.maxSpeed);
		blob.setRadiuses({ data.minRadius, data.idleRadius });
		blob.generateBlob();
		_blobs.push_back(std::move(blob));
	}
}

float LinearBlobs::maxRadius() const {
	const auto maxOfRadiuses = [](const BlobData &d) {
		return std::max(d.idleRadius, std::max(d.maxRadius, d.minRadius));
	};
	const auto max = *ranges::max_element(
		_blobDatas,
		std::less<>(),
		maxOfRadiuses);
	return maxOfRadiuses(max);
}

int LinearBlobs::size() const {
	return _blobs.size();
}

void LinearBlobs::setRadiusesAt(
		rpl::producer<Blob::Radiuses> &&radiuses,
		int index) {
	Expects(index >= 0 && index < size());
	std::move(
		radiuses
	) | rpl::start_with_next([=](Blob::Radiuses r) {
		_blobs[index].setRadiuses(std::move(r));
	}, _lifetime);
}

Blob::Radiuses LinearBlobs::radiusesAt(int index) {
	Expects(index >= 0 && index < size());
	return _blobs[index].radiuses();
}

void LinearBlobs::setLevel(float value) {
	const auto to = std::min(_maxLevel, value) / _maxLevel;
	_levelValue.start(to);
}

void LinearBlobs::paint(Painter &p, const QBrush &brush, int width) {
	PainterHighQualityEnabler hq(p);
	const auto opacity = p.opacity();
	for (auto i = 0; i < _blobs.size(); i++) {
		const auto alpha = _blobDatas[i].alpha;
		if (alpha != 1.) {
			p.setOpacity(opacity * alpha);
		}
		_blobs[i].paint(p, brush, width);
		if (alpha != 1.) {
			p.setOpacity(opacity);
		}
	}
}

void LinearBlobs::updateLevel(crl::time dt) {
	const auto limitedDt = (dt > 20) ? kRateLimit : dt;
	_levelValue.update(limitedDt);

	const auto level = (float)currentLevel();
	for (auto i = 0; i < _blobs.size(); i++) {
		const auto &data = _blobDatas[i];
		_blobs[i].setRadiuses({
			data.minRadius,
			data.idleRadius + (data.maxRadius - data.idleRadius) * level });
		_blobs[i].update(
			_levelValue.current(),
			data.speedScale,
			limitedDt / kRateLimitF);
	}
}

float64 LinearBlobs::currentLevel() const {
	return _levelValue.current();
}

} // namespace Ui::Paint
