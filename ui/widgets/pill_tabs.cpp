// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/pill_tabs.h"

#include "ui/widgets/buttons.h"
#include "ui/qt_object_factory.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

namespace Ui {

PillTabs::PillTabs(
	QWidget *parent,
	const std::vector<QString> &labels,
	int activeIndex,
	const style::PillTabs &st)
: RpWidget(parent)
, _st(st)
, _labels(labels)
, _activeIndex(activeIndex) {
	resize(0, _st.height);

	if (_activeIndex > 0 && _activeIndex < int(_labels.size())) {
		// Compute initial position without animation.
		// Will be corrected on first resize.
		_animatedPosition = 0.;
	}

	setupButtons();

	paintRequest(
	) | rpl::on_next([=] {
		paint();
	}, lifetime());
}

void PillTabs::setActiveIndex(int index) {
	if (index < 0 || index >= int(_labels.size())) {
		return;
	}
	if (_activeIndex == index) {
		return;
	}
	const auto tabWidth = width() / int(_labels.size());
	const auto targetPos = float64(index * tabWidth);
	_animation.stop();
	_animation.start(
		[=](float64 v) {
			_animatedPosition = v;
			update();
		},
		_animatedPosition,
		targetPos,
		_st.duration,
		anim::easeOutQuint);
	_activeIndex = index;
	_activeIndexChanges.fire_copy(index);
}

int PillTabs::activeIndex() const {
	return _activeIndex;
}

rpl::producer<int> PillTabs::activeIndexChanges() const {
	return _activeIndexChanges.events();
}

void PillTabs::setupButtons() {
	const auto count = int(_labels.size());
	for (auto i = 0; i < count; ++i) {
		const auto btn = CreateChild<AbstractButton>(this);
		btn->setClickedCallback([=] {
			setActiveIndex(i);
		});
		sizeValue(
		) | rpl::on_next([=](QSize size) {
			const auto tabWidth = size.width() / count;
			btn->setGeometry(i * tabWidth, 0, tabWidth, size.height());
			if (i == _activeIndex && !_animation.animating()) {
				_animatedPosition = float64(i * tabWidth);
			}
		}, btn->lifetime());
	}
}

void PillTabs::paint() {
	if (!width() || !height()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto r = rect();
	const auto h = _st.height;
	const auto radius = h / 2.;
	const auto count = int(_labels.size());
	const auto tabWidth = r.width() / count;
	const auto bw = _st.borderWidth;

	// Background fill + border.
	auto pen = QPen(_st.bgActive);
	pen.setWidthF(bw);
	p.setPen(pen);
	p.setBrush(_st.bg);
	const auto hw = bw / 2.;
	p.drawRoundedRect(
		QRectF(hw, hw, r.width() - bw, r.height() - bw),
		radius,
		radius);

	// Active pill.
	p.setPen(Qt::NoPen);
	p.setBrush(_st.bgActive);
	p.drawRoundedRect(
		QRectF(_animatedPosition, 0, tabWidth, h),
		radius,
		radius);

	// Text.
	p.setFont(_st.textStyle.font);
	for (auto i = 0; i < count; ++i) {
		const auto active = (_activeIndex == i);
		p.setPen(active ? _st.fgActive : _st.fg);
		const auto textRect = QRect(i * tabWidth, 0, tabWidth, h);
		const auto elided = _st.textStyle.font->elided(
			_labels[i],
			tabWidth - h);
		p.drawText(textRect, elided, style::al_center);
	}
}

} // namespace Ui
