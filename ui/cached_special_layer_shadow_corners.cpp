// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/cached_special_layer_shadow_corners.h"

#include "ui/effects/ripple_animation.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

[[nodiscard]] std::array<QImage, 4> PrepareSpecialLayerShadowCorners() {
	const auto &st = st::boxRoundShadow;

	const auto s = QSize(
		st::boxRadius * 2 + st.extend.left(),
		st::boxRadius * 2 + st.extend.right());
	const auto mask = Ui::RippleAnimation::MaskByDrawer(s, false, [&](
			QPainter &p) {
		p.drawRoundedRect(QRect(QPoint(), s), st::boxRadius, st::boxRadius);
	});
	struct Corner {
		const style::icon &icon;
		QPoint factor;
	};
	const auto corners = std::vector<Corner>{
		Corner{ st.topLeft, QPoint(1, 1) },
		Corner{ st.bottomLeft, QPoint(1, 0) },
		Corner{ st.topRight, QPoint(0, 1) },
		Corner{ st.bottomRight, QPoint(0, 0) },
	};
	const auto processCorner = [&](int i) {
		const auto &corner = corners[i];
		auto result = QImage(
			corner.icon.size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(Qt::transparent);

		{
			QPainter p(&result);
			corner.icon.paint(p, 0, 0, corner.icon.width());
			p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
			p.drawImage(
				corner.icon.width() * corner.factor.x()
					- mask.width() / style::DevicePixelRatio() / 2,
				corner.icon.height() * corner.factor.y()
					- mask.height() / style::DevicePixelRatio() / 2,
				mask);
		}
		return result;
	};
	return std::array<QImage, 4>{ {
		processCorner(0),
		processCorner(1),
		processCorner(2),
		processCorner(3),
	} };
}

} // namespace

const std::array<QImage, 4> &SpecialLayerShadowCorners() {
	static const auto custom = PrepareSpecialLayerShadowCorners();
	return custom;
}

} // namespace Ui
