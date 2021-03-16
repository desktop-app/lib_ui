// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/tooltip.h"

#include "ui/ui_utility.h"
#include "ui/platform/ui_platform_utility.h"
#include "base/invoke_queued.h"
#include "base/qt_adapters.h"
#include "styles/style_widgets.h"

#include <QtGui/QScreen>
#include <QtWidgets/QApplication>

namespace Ui {

Tooltip *TooltipInstance = nullptr;

const style::Tooltip *AbstractTooltipShower::tooltipSt() const {
	return &st::defaultTooltip;
}

AbstractTooltipShower::~AbstractTooltipShower() {
	if (TooltipInstance && TooltipInstance->_shower == this) {
		TooltipInstance->_shower = 0;
	}
}

Tooltip::Tooltip() : RpWidget(nullptr) {
	TooltipInstance = this;

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::BypassWindowManagerHint | Qt::NoDropShadowWindowHint | Qt::ToolTip);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	_showTimer.setCallback([=] { performShow(); });
	_hideByLeaveTimer.setCallback([=] { Hide(); });
}

void Tooltip::performShow() {
	if (_shower) {
		auto text = _shower->tooltipWindowActive()
			? _shower->tooltipText()
			: QString();
		if (text.isEmpty()) {
			Hide();
		} else {
			TooltipInstance->popup(_shower->tooltipPos(), text, _shower->tooltipSt());
		}
	}
}

bool Tooltip::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Leave) {
		_hideByLeaveTimer.callOnce(10);
	} else if (e->type() == QEvent::Enter) {
		_hideByLeaveTimer.cancel();
	} else if (e->type() == QEvent::MouseMove) {
		if ((QCursor::pos() - _point).manhattanLength() > QApplication::startDragDistance()) {
			Hide();
		}
	}
	return RpWidget::eventFilter(o, e);
}

Tooltip::~Tooltip() {
	if (TooltipInstance == this) {
		TooltipInstance = nullptr;
	}
}

void Tooltip::popup(const QPoint &m, const QString &text, const style::Tooltip *st) {
	const auto screen = base::QScreenNearestTo(m);
	if (!screen) {
		Hide();
		return;
	}

	if (!_isEventFilter) {
		_isEventFilter = true;
		QCoreApplication::instance()->installEventFilter(this);
	}

	_point = m;
	_st = st;
	_text = Text::String(_st->textStyle, text, _textPlainOptions, _st->widthMax, true);

	_useTransparency = Platform::TranslucentWindowsSupported(_point);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);

	int32 addw = 2 * st::lineWidth + _st->textPadding.left() + _st->textPadding.right();
	int32 addh = 2 * st::lineWidth + _st->textPadding.top() + _st->textPadding.bottom();

	// count tooltip size
	QSize s(addw + _text.maxWidth(), addh + _text.minHeight());
	if (s.width() > _st->widthMax) {
		s.setWidth(addw + _text.countWidth(_st->widthMax - addw));
		s.setHeight(addh + _text.countHeight(s.width() - addw));
	}
	int32 maxh = addh + (_st->linesMax * _st->textStyle.font->height);
	if (s.height() > maxh) {
		s.setHeight(maxh);
	}

	// count tooltip position
	QPoint p(m + _st->shift);
	if (style::RightToLeft()) {
		p.setX(m.x() - s.width() - _st->shift.x());
	}
	if (s.width() < 2 * _st->shift.x()) {
		p.setX(m.x() - (s.width() / 2));
	}

	// adjust tooltip position
	const auto r = screen->availableGeometry();
	if (r.x() + r.width() - _st->skip < p.x() + s.width() && p.x() + s.width() > m.x()) {
		p.setX(qMax(r.x() + r.width() - int32(_st->skip) - s.width(), m.x() - s.width()));
	}
	if (r.x() + _st->skip > p.x() && p.x() < m.x()) {
		p.setX(qMin(m.x(), r.x() + int32(_st->skip)));
	}
	if (r.y() + r.height() - _st->skip < p.y() + s.height()) {
		p.setY(m.y() - s.height() - _st->skip);
	}
	if (r.y() > p.x()) {
		p.setY(qMin(m.y() + _st->shift.y(), r.y() + r.height() - s.height()));
	}

	setGeometry(QRect(p, s));

	_hideByLeaveTimer.cancel();
	show();
}

void Tooltip::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_useTransparency) {
		p.setPen(_st->textBorder);
		p.setBrush(_st->textBg);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1., height() - 1.), st::roundRadiusSmall, st::roundRadiusSmall);
	} else {
		p.fillRect(rect(), _st->textBg);

		p.fillRect(QRect(0, 0, width(), st::lineWidth), _st->textBorder);
		p.fillRect(QRect(0, height() - st::lineWidth, width(), st::lineWidth), _st->textBorder);
		p.fillRect(QRect(0, st::lineWidth, st::lineWidth, height() - 2 * st::lineWidth), _st->textBorder);
		p.fillRect(QRect(width() - st::lineWidth, st::lineWidth, st::lineWidth, height() - 2 * st::lineWidth), _st->textBorder);
	}
	int32 lines = qFloor((height() - 2 * st::lineWidth - _st->textPadding.top() - _st->textPadding.bottom()) / _st->textStyle.font->height);

	p.setPen(_st->textFg);
	_text.drawElided(p, st::lineWidth + _st->textPadding.left(), st::lineWidth + _st->textPadding.top(), width() - 2 * st::lineWidth - _st->textPadding.left() - _st->textPadding.right(), lines);
}

void Tooltip::hideEvent(QHideEvent *e) {
	if (TooltipInstance == this) {
		Hide();
	}
}

void Tooltip::Show(int32 delay, const AbstractTooltipShower *shower) {
	if (!TooltipInstance) {
		new Tooltip();
	}
	TooltipInstance->_shower = shower;
	if (delay >= 0) {
		TooltipInstance->_showTimer.callOnce(delay);
	} else {
		TooltipInstance->performShow();
	}
}

void Tooltip::Hide() {
	if (auto instance = TooltipInstance) {
		TooltipInstance = nullptr;
		instance->_showTimer.cancel();
		instance->_hideByLeaveTimer.cancel();
		instance->hide();
		InvokeQueued(instance, [=] { instance->deleteLater(); });
	}
}

ImportantTooltip::ImportantTooltip(QWidget *parent, object_ptr<TWidget> content, const style::ImportantTooltip &st) : TWidget(parent)
, _st(st)
, _content(std::move(content)) {
	_content->setParent(this);
	_hideTimer.setCallback([this] { toggleAnimated(false); });
	hide();
}

void ImportantTooltip::pointAt(QRect area, RectParts side) {
	if (_area == area && _side == side) {
		return;
	}
	setArea(area);
	countApproachSide(side);
	updateGeometry();
	update();
}

void ImportantTooltip::setArea(QRect area) {
	Expects(parentWidget() != nullptr);
	_area = area;
	auto point = parentWidget()->mapToGlobal(_area.center());
	_useTransparency = Platform::TranslucentWindowsSupported(point);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);

	auto contentWidth = parentWidget()->rect().marginsRemoved(_st.padding).width();
	accumulate_min(contentWidth, _content->naturalWidth());
	_content->resizeToWidth(contentWidth);

	auto size = _content->rect().marginsAdded(_st.padding).size();
	if (_useTransparency) {
		size.setHeight(size.height() + _st.arrow);
	}
	if (size.width() < 2 * (_st.arrowSkipMin + _st.arrow)) {
		size.setWidth(2 * (_st.arrowSkipMin + _st.arrow));
	}
	resize(size);
}

void ImportantTooltip::countApproachSide(RectParts preferSide) {
	Expects(parentWidget() != nullptr);
	auto requiredSpace = countInner().height() + _st.shift;
	if (_useTransparency) {
		requiredSpace += _st.arrow;
	}
	auto available = parentWidget()->rect();
	auto availableAbove = _area.y() - available.y();
	auto availableBelow = (available.y() + available.height()) - (_area.y() + _area.height());
	auto allowedAbove = (availableAbove >= requiredSpace + _st.margin.top());
	auto allowedBelow = (availableBelow >= requiredSpace + _st.margin.bottom());
	if ((allowedAbove && allowedBelow) || (!allowedAbove && !allowedBelow)) {
		_side = preferSide;
	} else {
		_side = (allowedAbove ? RectPart::Top : RectPart::Bottom)
			| (preferSide & (RectPart::Left | RectPart::Center | RectPart::Right));
	}
	if (_useTransparency) {
		auto arrow = QImage(
			QSize(_st.arrow * 2, _st.arrow) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		arrow.fill(Qt::transparent);
		arrow.setDevicePixelRatio(style::DevicePixelRatio());
		{
			Painter p(&arrow);
			PainterHighQualityEnabler hq(p);

			QPainterPath path;
			path.moveTo(0, 0);
			path.lineTo(2 * _st.arrow, 0);
			path.lineTo(_st.arrow, _st.arrow);
			path.lineTo(0, 0);
			p.fillPath(path, _st.bg);
		}
		if (_side & RectPart::Bottom) {
			arrow = std::move(arrow).transformed(QTransform(1, 0, 0, -1, 0, 0));
		}
		_arrow = PixmapFromImage(std::move(arrow));
	}
}

void ImportantTooltip::toggleAnimated(bool visible) {
	if (_visible == isHidden()) {
		setVisible(_visible);
	}
	if (_visible != visible) {
		updateGeometry();
		_visible = visible;
		refreshAnimationCache();
		if (_visible) {
			show();
		} else if (isHidden()) {
			return;
		}
		hideChildren();
		_visibleAnimation.start([this] { animationCallback(); }, _visible ? 0. : 1., _visible ? 1. : 0., _st.duration, anim::easeOutCirc);
	}
}

void ImportantTooltip::hideAfter(crl::time timeout) {
	_hideTimer.callOnce(timeout);
}

void ImportantTooltip::animationCallback() {
	updateGeometry();
	update();
	checkAnimationFinish();
}

void ImportantTooltip::refreshAnimationCache() {
	if (_cache.isNull() && _useTransparency) {
		auto animation = base::take(_visibleAnimation);
		auto visible = std::exchange(_visible, true);
		showChildren();
		_cache = GrabWidget(this);
		_visible = base::take(visible);
		_visibleAnimation = base::take(animation);
	}
}

void ImportantTooltip::toggleFast(bool visible) {
	if (_visible == isHidden()) {
		setVisible(_visible);
	}
	if (_visibleAnimation.animating() || _visible != visible) {
		_visibleAnimation.stop();
		_visible = visible;
		checkAnimationFinish();
	}
}

void ImportantTooltip::checkAnimationFinish() {
	if (!_visibleAnimation.animating()) {
		_cache = QPixmap();
		showChildren();
		setVisible(_visible);
		if (_visible) {
			update();
		} else if (_hiddenCallback) {
			_hiddenCallback();
		}
	}
}

void ImportantTooltip::updateGeometry() {
	Expects(parentWidget() != nullptr);
	auto parent = parentWidget();
	auto areaMiddle = _area.x() + (_area.width() / 2);
	auto left = areaMiddle - (width() / 2);
	if (_side & RectPart::Left) {
		left = areaMiddle + _st.arrowSkip - width();
	} else if (_side & RectPart::Right) {
		left = areaMiddle - _st.arrowSkip;
	}
	accumulate_min(left, parent->width() - _st.margin.right() - width());
	accumulate_max(left, _st.margin.left());
	accumulate_max(left, areaMiddle + _st.arrow + _st.arrowSkipMin - width());
	accumulate_min(left, areaMiddle - _st.arrow - _st.arrowSkipMin);

	auto countTop = [this] {
		auto shift = anim::interpolate(_st.shift, 0, _visibleAnimation.value(_visible ? 1. : 0.));
		if (_side & RectPart::Top) {
			return _area.y() - height() - shift;
		}
		return _area.y() + _area.height() + shift;
	};
	move(left, countTop());
}

void ImportantTooltip::resizeEvent(QResizeEvent *e) {
	auto contentTop = _st.padding.top();
	if (_useTransparency && (_side & RectPart::Bottom)) {
		contentTop += _st.arrow;
	}
	_content->moveToLeft(_st.padding.left(), contentTop);
}

QRect ImportantTooltip::countInner() const {
	return _content->geometry().marginsAdded(_st.padding);
}

void ImportantTooltip::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto inner = countInner();
	if (_useTransparency) {
		if (!_cache.isNull()) {
			auto opacity = _visibleAnimation.value(_visible ? 1. : 0.);
			p.setOpacity(opacity);
			p.drawPixmap(0, 0, _cache);
		} else {
			if (!_visible) {
				return;
			}
			p.setBrush(_st.bg);
			p.setPen(Qt::NoPen);
			{
				PainterHighQualityEnabler hq(p);
				p.drawRoundedRect(inner, _st.radius, _st.radius);
			}
			auto areaMiddle = _area.x() + (_area.width() / 2) - x();
			auto arrowLeft = areaMiddle - _st.arrow;
			if (_side & RectPart::Top) {
				p.drawPixmapLeft(arrowLeft, inner.y() + inner.height(), width(), _arrow);
			} else {
				p.drawPixmapLeft(arrowLeft, inner.y() - _st.arrow, width(), _arrow);
			}
		}
	} else {
		p.fillRect(inner, QColor(_st.bg->c.red(), _st.bg->c.green(), _st.bg->c.blue()));
	}
}

} // namespace Ui
