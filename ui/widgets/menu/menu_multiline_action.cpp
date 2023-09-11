// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_multiline_action.h"

#include "ui/widgets/labels.h"

namespace Ui::Menu {

MultilineAction::MultilineAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	const style::FlatLabel &stLabel,
	QPoint labelPosition,
	TextWithEntities &&about,
	const style::icon *icon,
	const style::icon *iconOver)
: ItemBase(parent, st)
, _st(st)
, _icon(icon)
, _iconOver(iconOver ? iconOver : icon)
, _labelPosition(labelPosition)
, _text(base::make_unique_q<Ui::FlatLabel>(
	this,
	rpl::single(std::move(about)),
	stLabel))
, _dummyAction(Ui::CreateChild<QAction>(parent.get())) {
	ItemBase::enableMouseSelecting();
	_text->setAttribute(Qt::WA_TransparentForMouseEvents);
	updateMinWidth();
	parent->widthValue() | rpl::start_with_next([=](int width) {
		const auto top = _labelPosition.y();
		const auto skip = _labelPosition.x();
		_text->resizeToWidth(width - 2 * skip);
		_text->moveToLeft(skip, top);
		resize(width, contentHeight());
	}, lifetime());
}

not_null<QAction*> MultilineAction::action() const {
	return _dummyAction;
}

bool MultilineAction::isEnabled() const {
	return true;
}

int MultilineAction::contentHeight() const {
	const auto skip = _labelPosition.y();
	return skip
		+ std::max(_text->height(), _icon ? _icon->height() : 0)
		+ skip;
}

void MultilineAction::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto selected = isSelected();
	p.fillRect(rect(), selected ? _st.itemBgOver : _st.itemBg);
	RippleButton::paintRipple(p, 0, 0);
	if (const auto icon = (selected ? _iconOver : _icon)) {
		icon->paint(p, _st.itemIconPosition, width());
	}
}

void MultilineAction::updateMinWidth() {
	const auto skip = _labelPosition.x();
	auto min = _text->textMaxWidth() / 2;
	auto max = _icon ? _st.widthMax : (_text->textMaxWidth() - skip);
	_text->resizeToWidth(max);
	const auto height = _icon
		? ((_st.itemIconPosition.y() * 2) + _icon->height())
		: _text->height();
	_text->resizeToWidth(min);
	const auto heightMax = _text->height();
	if (heightMax > height) {
		while (min + 1 < max) {
			const auto middle = (max + min) / 2;
			_text->resizeToWidth(middle);
			if (_text->height() > height) {
				min = middle;
			} else {
				max = middle;
			}
		}
	}
	ItemBase::setMinWidth(skip * 2 + max);
}

} // namespace Ui::Menu
