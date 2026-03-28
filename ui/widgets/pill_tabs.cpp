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
	const auto tabWidth = (width() - rect::m::sum::h(m))
		/ int(_labels.size());
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
	if (_showOpacity < 1.) {
		p.setOpacity(_showOpacity);
	}
	auto hq = PainterHighQualityEnabler(p);

	const auto r = rect() - _shadowMargins;
	const auto h = _st.height;
	const auto radius = h / 2.;
	const auto count = int(_labels.size());
	const auto tabWidth = r.width() / count;

	if (_showProgress < 1.) {
		const auto visibleWidth = int(r.width() * _showProgress);
		const auto visibleRect = QRect(r.x(), r.y(), visibleWidth, h);

		// Shadow around the visible portion only.
		if (_shadow && visibleWidth > 0) {
			_shadow->paint(p, visibleRect, radius);
		}

		// Content clip: tight pill shape.
		auto contentClip = QPainterPath();
		contentClip.addRoundedRect(
			QRectF(r.x(), r.y(), visibleWidth, h),
			radius,
			radius);
		p.setClipPath(contentClip);
	} else if (_shadow) {
		_shadow->paint(p, r, radius);
	}

	// Background fill + border.
	const auto bw = _st.borderWidth;
	auto pen = QPen(_st.bgActive);
	pen.setWidthF(bw);
	p.setPen(pen);
	p.setBrush(_st.bg);
	const auto hw = bw / 2.;
	p.drawRoundedRect(
		QRectF(r.x() + hw, r.y() + hw, r.width() - bw, r.height() - bw),
		radius,
		radius);

	// Active pill.
	p.setPen(Qt::NoPen);
	p.setBrush(_st.bgActive);
	p.drawRoundedRect(
		QRectF(r.x() + _animatedPosition, r.y(), tabWidth, h),
		radius,
		radius);

	// Text.
	p.setFont(_st.textStyle.font);
	for (auto i = 0; i < count; ++i) {
		const auto active = (_activeIndex == i);
		p.setPen(active ? _st.fgActive : _st.fg);
		const auto textRect = QRect(
			r.x() + i * tabWidth,
			r.y(),
			tabWidth,
			h);
		const auto elided = _st.textStyle.font->elided(
			_labels[i],
			tabWidth - h);
		p.drawText(textRect, elided, style::al_center);
	}
}

} // namespace Ui
