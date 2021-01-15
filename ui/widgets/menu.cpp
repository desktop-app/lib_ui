// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu.h"

#include "ui/effects/ripple_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/text/text.h"

#include <QtGui/QtEvents>

namespace Ui {
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

class Menu::Separator : public ItemBase {
public:
	Separator(not_null<RpWidget*> parent, const style::Menu &st, int index)
	: ItemBase(parent, st, index)
	, _lineWidth(st.separatorWidth)
	, _padding(st.separatorPadding)
	, _fg(st.separatorFg)
	, _bg(st.itemBg)
	, _height(_padding.top() + _lineWidth + _padding.bottom()) {

		initResizeHook(parent->sizeValue());
		// setAttribute(Qt::WA_TransparentForMouseEvents, true);
		paintRequest(
		) | rpl::start_with_next([=] {
			Painter p(this);

			p.fillRect(0, 0, width(), _height, _bg);
			p.fillRect(
				_padding.left(),
				_padding.top(),
				width() - _padding.left() - _padding.right(),
				_lineWidth,
				_fg);
		}, lifetime());

	}

	QAction *action() const override {
		return nullptr;
	}

	bool isEnabled() const override {
		return false;
	}

protected:
	int contentHeight() const override {
		return _height;
	}

private:
	const int _lineWidth;
	const style::margins &_padding;
	const style::color &_fg;
	const style::color &_bg;
	const int _height;

};

class Menu::Action : public ItemBase {
public:
	Action(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		int index,
		not_null<QAction*> action,
		const style::icon *icon,
		const style::icon *iconOver,
		bool hasSubmenu)
	: ItemBase(parent, st, index)
	, _action(action)
	, _st(st)
	, _icon(icon)
	, _iconOver(iconOver)
	, _height(_st.itemPadding.top()
		+ _st.itemStyle.font->height
		+ _st.itemPadding.bottom()) {

		initResizeHook(parent->sizeValue());
		processAction();
		setHasSubmenu(hasSubmenu);

		paintRequest(
		) | rpl::start_with_next([=] {
			Painter p(this);
			paint(p);
		}, lifetime());

		events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return _action->isEnabled()
				&& ((e->type() == QEvent::Leave)
					|| (e->type() == QEvent::Enter));
		}) | rpl::map([=](not_null<QEvent*> e) {
			return (e->type() == QEvent::Enter);
		}) | rpl::start_with_next([=](bool selected) {
			setSelected(selected);
		}, lifetime());

		events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return _action->isEnabled() && (e->type() == QEvent::MouseMove);
		}) | rpl::start_with_next([=](not_null<QEvent*> e) {
			setSelected(true);
		}, lifetime());

		connect(_action, &QAction::changed, [=] { processAction(); });
	}

void paint(Painter &p) {

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
		p.setPen(selected ? _st.itemFgShortcutOver : (enabled ? _st.itemFgShortcut : _st.itemFgShortcutDisabled));
		p.drawTextRight(_st.itemPadding.right(), _st.itemPadding.top(), width(), _shortcut);
	}
}

void processAction() {
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
	// if (action->isCheckable()) {
	// 	auto updateCallback = [this, index] { updateItem(index); };
	// 	if (_toggle) {
	// 		_toggle->setUpdateCallback(updateCallback);
	// 		_toggle->setChecked(action->isChecked(), anim::type::normal);
	// 	} else {
	// 		_toggle = std::make_unique<ToggleView>(_st.itemToggle, action->isChecked(), updateCallback);
	// 	}
	// 	goodWidth += _st.itemPadding.right() + _toggle->getSize().width() - _st.itemToggleShift;
	// } else {
	// 	_toggle.reset();
	// }
	const auto w = std::clamp(goodWidth, _st.widthMin, _st.widthMax);
	_textWidth = w - (goodWidth - textWidth);
	_shortcut = actionShortcut;
	setContentWidth(w);
	update();
}

bool isEnabled() const override {
	return _action->isEnabled();
}

QAction *action() const override {
	return _action;
}

protected:
QPoint prepareRippleStartPosition() const override {
	return mapFromGlobal(QCursor::pos());
}

QImage prepareRippleMask() const override {
	return RippleAnimation::rectMask(size());
}

protected:
	int contentHeight() const override {
		return _height;
	}

private:

	Text::String _text;
	QString _shortcut;
	const not_null<QAction*> _action;
	const style::Menu &_st;
	const style::icon *_icon;
	const style::icon *_iconOver;
	// std::unique_ptr<RippleAnimation> _ripple;
	std::unique_ptr<ToggleView> _toggle;
	int _textWidth = 0;
	const int _height;

	// rpl::variable<bool> _selected = false;

};

Menu::Menu(QWidget *parent, const style::Menu &st)
: RpWidget(parent)
, _st(st) {
	init();
}

Menu::Menu(QWidget *parent, QMenu *menu, const style::Menu &st)
: RpWidget(parent)
, _st(st)
, _wappedMenu(menu) {
	init();

	_wappedMenu->setParent(this);
	for (auto action : _wappedMenu->actions()) {
		addAction(action);
	}
	_wappedMenu->hide();
}

Menu::~Menu() = default;

void Menu::init() {
	resize(_forceWidth ? _forceWidth : _st.widthMin, _st.skip * 2);

	setMouseTracking(true);

	if (_st.itemBg->c.alpha() == 255) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);
		p.fillRect(clip, _st.itemBg);
	}, lifetime());
}

not_null<QAction*> Menu::addAction(const QString &text, Fn<void()> callback, const style::icon *icon, const style::icon *iconOver) {
	const auto action = addAction(new QAction(text, this), icon, iconOver);
	connect(action, &QAction::triggered, action, std::move(callback), Qt::QueuedConnection);
	return action;
}

not_null<QAction*> Menu::addAction(const QString &text, std::unique_ptr<QMenu> submenu) {
	const auto action = new QAction(text, this);
	action->setMenu(submenu.release());
	return addAction(action, nullptr, nullptr);
}

not_null<QAction*> Menu::addAction(not_null<QAction*> action, const style::icon *icon, const style::icon *iconOver) {
	_actions.emplace_back(action);

	const auto top = _actionWidgets.empty()
		? 0
		: _actionWidgets.back()->y() + _actionWidgets.back()->height();
	const auto index = _actionWidgets.size();
	if (action->isSeparator()) {
		auto widget = base::make_unique_q<Separator>(this, _st, index);
		widget->moveToLeft(0, top);
		widget->show();
		_actionWidgets.push_back(std::move(widget));
	} else {
		auto widget = base::make_unique_q<Action>(
			this,
			_st,
			index,
			action,
			icon,
			iconOver ? iconOver : icon,
			(action->menu() != nullptr));
		widget->moveToLeft(0, top);
		widget->show();

		widget->selects(
		) | rpl::start_with_next([=, w = widget.get()](bool selected) {
			if (!selected) {
				return;
			}
			for (auto i = 0; i < _actionWidgets.size(); i++) {
				if (_actionWidgets[i].get() != w) {
					_actionWidgets[i]->setSelected(false);
				}
			}
			if (_activatedCallback) {
				_activatedCallback(
					w->action(),
					w->y(),
					w->lastTriggeredSource());
			}
		}, widget->lifetime());

		widget->clicks(
		) | rpl::start_with_next([=, w = widget.get()]() {
			if (_triggeredCallback) {
				_triggeredCallback(w->action(), w->y(), w->lastTriggeredSource());
			}
		}, widget->lifetime());

		widget->contentWidthValue(
		) | rpl::start_with_next([=] {
			const auto newWidth = _forceWidth
				? _forceWidth
				: _actionWidgets.empty()
				? _st.widthMin
				: (*ranges::max_element(
					_actionWidgets,
					std::greater<>(),
					&ItemBase::width))->contentWidth();
			resize(newWidth, height());
		}, widget->lifetime());

		_actionWidgets.push_back(std::move(widget));
	}


	const auto newHeight = ranges::accumulate(
		_actionWidgets,
		0,
		ranges::plus(),
		&ItemBase::height);
	resize(width(), newHeight);
	updateSelected(QCursor::pos());

	return action;
}

not_null<QAction*> Menu::addSeparator() {
	const auto separator = new QAction(this);
	separator->setSeparator(true);
	return addAction(separator);
}

void Menu::clearActions() {
	setSelected(-1);
	_actionWidgets.clear();
	for (auto action : base::take(_actions)) {
		if (action->parent() == this) {
			delete action;
		}
	}
	resize(_forceWidth ? _forceWidth : _st.widthMin, _st.skip * 2);
}

void Menu::finishAnimating() {
}

void Menu::setShowSource(TriggeredSource source) {
	_mouseSelection = (source == TriggeredSource::Mouse);
	setSelected((source == TriggeredSource::Mouse || _actions.empty()) ? -1 : 0);
}

const std::vector<not_null<QAction*>> &Menu::actions() const {
	return _actions;
}

void Menu::setForceWidth(int forceWidth) {
	_forceWidth = forceWidth;
	resize(_forceWidth, height());
}

void Menu::updateSelected(QPoint globalPosition) {
	if (!_mouseSelection) {
		return;
	}

	const auto p = mapFromGlobal(globalPosition) - QPoint(0, _st.skip);
	for (const auto &widget : _actionWidgets) {
		const auto widgetRect = QRect(widget->pos(), widget->size());
		if (widgetRect.contains(p)) {
			widget->setSelected(true);
			break;
		}
	}
}

void Menu::itemPressed(TriggeredSource source) {
	if (const auto action = findSelectedAction()) {
		if (action->lastTriggeredSource() == source) {
			action->setClicked(source);
		}
	}
}

void Menu::keyPressEvent(QKeyEvent *e) {
	const auto key = e->key();
	if (!_keyPressDelegate || !_keyPressDelegate(key)) {
		handleKeyPress(key);
	}
}

ItemBase *Menu::findSelectedAction() const {
	const auto it = ranges::find_if(_actionWidgets, &ItemBase::isSelected);
	return (it == end(_actionWidgets)) ? nullptr : it->get();
}

void Menu::handleKeyPress(int key) {
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		itemPressed(TriggeredSource::Keyboard);
		return;
	}
	if (key == (style::RightToLeft() ? Qt::Key_Left : Qt::Key_Right)) {
		if (_selected >= 0 && _actionWidgets[_selected]->hasSubmenu()) {
			itemPressed(TriggeredSource::Keyboard);
			return;
		} else if (_selected < 0 && !_actions.empty()) {
			_mouseSelection = false;
			setSelected(0);
		}
	}
	if ((key != Qt::Key_Up && key != Qt::Key_Down) || _actions.empty()) {
		return;
	}

	const auto delta = (key == Qt::Key_Down ? 1 : -1);
	const auto selected = findSelectedAction();
	auto start = selected ? selected->index() : -1;
	if (start < 0 || start >= _actions.size()) {
		start = (delta > 0) ? (_actions.size() - 1) : 0;
	}
	auto newSelected = start;
	do {
		newSelected += delta;
		if (newSelected < 0) {
			newSelected += _actions.size();
		} else if (newSelected >= _actions.size()) {
			newSelected -= _actions.size();
		}
	} while (newSelected != start && (!_actionWidgets[newSelected]->isEnabled()));

	if (_actionWidgets[newSelected]->isEnabled()) {
		_mouseSelection = false;
		setSelected(newSelected);
	}
}

void Menu::clearSelection() {
	_mouseSelection = false;
	setSelected(-1);
}

void Menu::clearMouseSelection() {
	if (_mouseSelection && !_childShown) {
		clearSelection();
	}
}

void Menu::setSelected(int selected) {
	if (selected >= _actions.size()) {
		selected = -1;
	}
	if (_selected != selected) {
		const auto source = _mouseSelection
			? TriggeredSource::Mouse
			: TriggeredSource::Keyboard;
		// updateSelectedItem();
		// if (_selected >= 0 && _selected != _pressed && _actionsData[_selected].toggle) {
		// 	_actionsData[_selected].toggle->setStyle(_st.itemToggle);
		// }
		if (_selected >= 0) {
			_actionWidgets[_selected]->setSelected(false, source);
		}
		_selected = selected;
		if (_selected >= 0) {
			_actionWidgets[_selected]->setSelected(true, source);
		}
		// if (_selected >= 0 && _actionsData[_selected].toggle && _actions[_selected]->isEnabled()) {
		// 	_actionsData[_selected].toggle->setStyle(_st.itemToggleOver);
		// }
		// updateSelectedItem();
		// if (_activatedCallback) {
		// 	auto source = _mouseSelection ? TriggeredSource::Mouse : TriggeredSource::Keyboard;
		// 	_activatedCallback(
		// 		(_selected >= 0) ? _actions[_selected].get() : nullptr,
		// 		itemTop(_selected),
		// 		source);
		// }
	}
}

void Menu::mouseMoveEvent(QMouseEvent *e) {
	handleMouseMove(e->globalPos());
}

void Menu::handleMouseMove(QPoint globalPosition) {
	const auto margins = style::margins(0, _st.skip, 0, _st.skip);
	const auto inner = rect().marginsRemoved(margins);
	const auto localPosition = mapFromGlobal(globalPosition);
	if (inner.contains(localPosition)) {
		_mouseSelection = true;
		updateSelected(globalPosition);
	} else {
		clearMouseSelection();
		if (_mouseMoveDelegate) {
			_mouseMoveDelegate(globalPosition);
		}
	}
}

void Menu::mousePressEvent(QMouseEvent *e) {
	handleMousePress(e->globalPos());
}

void Menu::mouseReleaseEvent(QMouseEvent *e) {
	handleMouseRelease(e->globalPos());
}

void Menu::handleMousePress(QPoint globalPosition) {
	handleMouseMove(globalPosition);
	if (_mousePressDelegate) {
		_mousePressDelegate(globalPosition);
	}
}

void Menu::handleMouseRelease(QPoint globalPosition) {
	if (!rect().contains(mapFromGlobal(globalPosition))
			&& _mouseReleaseDelegate) {
		_mouseReleaseDelegate(globalPosition);
	}
}

} // namespace Ui
