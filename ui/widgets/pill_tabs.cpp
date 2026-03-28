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
#include "ui/rect.h"
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
	const auto m = _shadowMargins;
	const auto bw = _st.borderWidth;
	const auto innerWidth = width() - rect::m::sum::h(m) - 2 * bw;
	const auto tabWidth = float64(innerWidth) / int(_labels.size());
	const auto targetPos = index * tabWidth;
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

void PillTabs::setShowProgress(float64 progress, float64 opacity) {
	_showProgress = progress;
	_showOpacity = opacity;
	update();
}

void PillTabs::setShadow(const style::BoxShadow &st) {
	_shadow.emplace(st);
	_shadowMargins = _shadow->extend();
	update();
}

QMargins PillTabs::shadowExtend() const {
	return _shadow ? _shadow->extend() : QMargins();
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
			const auto inner = Rect(size) - _shadowMargins;
			const auto tabWidth = inner.width() / count;
			btn->setGeometry(
				inner.x() + i * tabWidth,
				inner.y(),
				tabWidth,
				inner.height());
			if (i == _activeIndex && !_animation.animating()) {
				const auto bw = _st.borderWidth;
				_animatedPosition = i
					* (float64(inner.width() - 2 * bw) / count);
			}
		}, btn->lifetime());
	}
}

void PillTabs::paint() {
	if (!width() || !height()) {
		return;
	}
	auto p = QPainter(this);
	if (_showOpacity < 1.) {
		p.setOpacity(_showOpacity);
	}
	auto hq = PainterHighQualityEnabler(p);

	const auto r = rect() - _shadowMargins;
	const auto h = _st.height;
	const auto radius = h / 2.;
	const auto count = int(_labels.size());

	if (_showProgress < 1.) {
		const auto visibleWidth = int(r.width() * _showProgress);
		const auto visibleRect = QRect(r.x(), r.y(), visibleWidth, h);

		if (_shadow && visibleWidth > 0) {
			_shadow->paint(p, visibleRect, radius);
		}

		auto contentClip = QPainterPath();
		contentClip.addRoundedRect(
			QRectF(r.x(), r.y(), visibleWidth, h),
			radius,
			radius);
		p.setClipPath(contentClip);
	} else if (_shadow) {
		_shadow->paint(p, r, radius);
	}

	// Background border (outer frame).
	const auto bw = _st.borderWidth;
	p.setPen(Qt::NoPen);
	p.setBrush(_st.bgBorder);
	p.drawRoundedRect(QRectF(r), radius, radius);

	// Inner area (inside border).
	const auto ir = QRectF(
		r.x() + bw,
		r.y() + bw,
		r.width() - 2 * bw,
		r.height() - 2 * bw);
	const auto iradius = radius - bw;
	const auto tabWidth = ir.width() / count;

	// Background fill.
	p.setBrush(_st.bg);
	p.drawRoundedRect(ir, iradius, iradius);

	// Active pill (with overlap into neighbors).
	const auto overlap = bw;
	const auto pillLeft = std::max(
		ir.x(),
		ir.x() + _animatedPosition - overlap);
	const auto pillRight = std::min(
		ir.x() + ir.width(),
		ir.x() + _animatedPosition + tabWidth + overlap);
	p.setBrush(_st.bgActive);
	p.drawRoundedRect(
		QRectF(pillLeft, ir.y(), pillRight - pillLeft, ir.height()),
		iradius,
		iradius);

	// Text.
	p.setFont(_st.textStyle.font);
	for (auto i = 0; i < count; ++i) {
		const auto active = (_activeIndex == i);
		p.setPen(active ? _st.fgActive : _st.fg);
		const auto left = int(std::round(r.x() + i * (r.width() / count)));
		const auto right = int(std::round(
			r.x() + (i + 1) * (r.width() / count)));
		const auto textRect = QRect(left, r.y(), right - left, h);
		const auto elided = _st.textStyle.font->elided(
			_labels[i],
			textRect.width() - h);
		p.drawText(textRect, elided, style::al_center);
	}
}

} // namespace Ui
