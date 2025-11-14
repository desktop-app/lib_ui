// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/custom_emoji_text_badge.h"

#include "ui/text/text.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

namespace Ui::Text {

[[nodiscard]] PaletteDependentEmoji CustomEmojiTextBadge(
		const QString &text,
		const style::RoundButton &st,
		const style::margins &margin) {
	return { .factory = [=, &st] {
		auto string = Ui::Text::String(st.style, text.toUpper());
		const auto size = QSize(string.maxWidth(), string.minHeight());
		const auto full = QSize(
			(st.width < 0) ? (size.width() - st.width) : st.width,
			st.height);
		const auto ratio = style::DevicePixelRatio();

		auto result = QImage(
			full * ratio,
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		result.fill(Qt::transparent);

		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st.textBg);

		const auto r = st.radius;
		p.drawRoundedRect(0, 0, full.width(), full.height(), r, r);

		const auto x = (full.width() - size.width()) / 2;
		p.setPen(st.textFg);
		string.draw(p, {
			.position = { x, st.textTop },
			.availableWidth = size.width(),
		});

		p.end();
		return result;
	}, .margin = margin };
}

}