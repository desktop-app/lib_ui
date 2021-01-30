// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu_action.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"

#include <QtGui/QtEvents>

namespace Ui::Menu {
namespace {

[[nodiscard]] TextWithEntities ParseMenuItem(const QString &text) {
	auto result = TextWithEntities();
	result.text.reserve(text.size());
	auto afterAmpersand = false;
	for (const auto ch : text) {
		if (afterAmpersand) {
			afterAmpersand = false;
			if (ch == '&') {
				result.text.append(ch);
			} else {
				result.entities.append(EntityInText{
					EntityType::Underline,
					result.text.size(),
					1 });
				result.text.append(ch);
			}
		} else if (ch == '&') {
			afterAmpersand = true;
		} else {
			result.text.append(ch);
		}
	}
	return result;
}

TextParseOptions MenuTextOptions = {
	TextParseLinks | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

} // namespace

Action::Action(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	not_null<QAction*> action,
	const style::icon *icon,
	const style::icon *iconOver)
: ItemBase(parent, st)
, _action(action)
, _st(st)
, _icon(icon)
, _iconOver(iconOver)
, _height(_st.itemPadding.top()
	+ _st.itemStyle.font->height
	+ _st.itemPadding.bottom()) {

	setAcceptBoth(true);

	initResizeHook(parent->sizeValue());
	processAction();

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();

	connect(_action, &QAction::changed, [=] { processAction(); });
}

bool Action::hasSubmenu() const {
	return _action->menu() != nullptr;
}

void Action::paint(Painter &p) {
	const auto enabled = _action->isEnabled();
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}
	if (const auto icon = (selected ? _iconOver : _icon)) {
		icon->paint(p, _st.itemIconPosition, width());
	}
	p.setPen(selected ? _st.itemFgOver : (enabled ? _st.itemFg : _st.itemFgDisabled));
	_text.drawLeftElided(p, _st.itemPadding.left(), _st.itemPadding.top(), _textWidth, width());
	if (hasSubmenu()) {
		const auto left = width() - _st.itemPadding.right() - _st.arrow.width();
		const auto top = (_height - _st.arrow.height()) / 2;
		if (enabled) {
			_st.arrow.paint(p, left, top, width());
		} else {
			_st.arrow.paint(
				p,
				left,
				top,
				width(),
				_st.itemFgDisabled->c);
		}
	} else if (!_shortcut.isEmpty()) {
		p.setPen(selected
			? _st.itemFgShortcutOver
			: (enabled ? _st.itemFgShortcut : _st.itemFgShortcutDisabled));
		p.drawTextRight(
			_st.itemPadding.right(),
			_st.itemPadding.top(),
			width(),
			_shortcut);
	}
}

void Action::processAction() {
	if (_action->text().isEmpty()) {
		_shortcut = QString();
		_text.clear();
		return;
	}
	const auto actionTextParts = _action->text().split('\t');
	const auto actionText = actionTextParts.empty()
		? QString()
		: actionTextParts[0];
	const auto actionShortcut = (actionTextParts.size() > 1)
		? actionTextParts[1]
		: QString();
	_text.setMarkedText(
		_st.itemStyle,
		ParseMenuItem(actionText),
		MenuTextOptions);
	const auto textWidth = _text.maxWidth();
	const auto &padding = _st.itemPadding;

	const auto additionalWidth = hasSubmenu()
		? padding.right() + _st.arrow.width()
		: (!actionShortcut.isEmpty())
		? (padding.right() + _st.itemStyle.font->width(actionShortcut))
		: 0;
	const auto goodWidth = padding.left()
		+ textWidth
		+ padding.right()
		+ additionalWidth;

	const auto w = std::clamp(goodWidth, _st.widthMin, _st.widthMax);
	_textWidth = w - (goodWidth - textWidth);
	_shortcut = actionShortcut;
	setMinWidth(w);
	update();
}

bool Action::isEnabled() const {
	return _action->isEnabled();
}

not_null<QAction*> Action::action() const {
	return _action;
}

QPoint Action::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage Action::prepareRippleMask() const {
	return Ui::RippleAnimation::rectMask(size());
}

int Action::contentHeight() const {
	return _height;
}

void Action::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(TriggeredSource::Keyboard);
		return;
	}
	if (key == (style::RightToLeft() ? Qt::Key_Left : Qt::Key_Right)) {
		if (hasSubmenu()) {
			setClicked(TriggeredSource::Keyboard);
			return;
		}
	}
}

} // namespace Ui::Menu
