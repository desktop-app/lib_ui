// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_multiline_action.h"

#include "ui/widgets/labels.h"
#include "ui/qt_object_factory.h"

namespace Ui::Menu {

MultilineAction::MultilineAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	const style::FlatLabel &stLabel,
	QPoint labelPosition,
	TextWithEntities &&about,
	const style::icon *icon,
	const style::icon *iconOver)
: MultilineAction(
	parent,
	st,
	stLabel,
	labelPosition,
	std::move(about),
	Text::MarkedContext(),
	icon,
	iconOver) {
}

MultilineAction::MultilineAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	const style::FlatLabel &stLabel,
	QPoint labelPosition,
	TextWithEntities &&about,
	const Text::MarkedContext &context,
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
	stLabel,
	st::defaultPopupMenu,
	context))
, _dummyAction(Ui::CreateChild<QAction>(parent.get())) {
	ItemBase::enableMouseSelecting();
	_text->setAttribute(Qt::WA_TransparentForMouseEvents);
	updateMinWidth();
	parent->widthValue() | rpl::on_next([=](int width) {
		const auto top = _labelPosition.y();
		const auto skip = _labelPosition.x();
		const auto rightSkip = _icon ? _st.itemIconPosition.x() : skip;
		_text->resizeToWidth(width - skip - rightSkip);
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
		+ std::max(_text->heightNoMargins(), _icon ? _icon->height() : 0)
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
	const auto rightSkip = _icon ? _st.itemIconPosition.x() : skip;
	auto min = _text->textMaxWidth() / 4;
	auto max = _icon ? _st.widthMax : (_text->textMaxWidth() - skip);
	_text->resizeToWidth(max);
	const auto height = _icon
		? ((_st.itemIconPosition.y() * 2) + _icon->height())
		: _text->heightNoMargins();
	_text->resizeToWidth(min);
	const auto heightMax = _text->heightNoMargins();
	if (heightMax > height) {
		while (min + 1 < max) {
			const auto middle = (max + min) / 2;
			_text->resizeToWidth(middle);
			if (_text->heightNoMargins() > height) {
				min = middle;
			} else {
				max = middle;
			}
		}
	}
	ItemBase::setMinWidth(skip + rightSkip + max);
}

} // namespace Ui::Menu
