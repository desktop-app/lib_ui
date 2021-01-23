// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/menu/menu.h"

#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/menu/menu_separator.h"

#include <QtGui/QtEvents>

namespace Ui::Menu {

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

not_null<QAction*> Menu::addAction(
		const QString &text,
		Fn<void()> callback,
		const style::icon *icon,
		const style::icon *iconOver) {
	auto action = CreateAction(this, text, std::move(callback));
	return addAction(std::move(action), icon, iconOver);
}

not_null<QAction*> Menu::addAction(
		const QString &text,
		std::unique_ptr<QMenu> submenu) {
	const auto action = new QAction(text, this);
	action->setMenu(submenu.release());
	return addAction(action, nullptr, nullptr);
}

not_null<QAction*> Menu::addAction(
		not_null<QAction*> action,
		const style::icon *icon,
		const style::icon *iconOver) {
	if (action->isSeparator()) {
		return addSeparator();
	}
	auto item = base::make_unique_q<Action>(
		this,
		_st,
		std::move(action),
		icon,
		iconOver ? iconOver : icon);
	return addAction(std::move(item));
}

not_null<QAction*> Menu::addAction(base::unique_qptr<ItemBase> widget) {
	const auto action = widget->action();
	_actions.emplace_back(action);

	widget->setParent(this);

	const auto top = _actionWidgets.empty()
		? 0
		: _actionWidgets.back()->y() + _actionWidgets.back()->height();

	widget->moveToLeft(0, top);
	widget->show();

	widget->setIndex(_actionWidgets.size());

	widget->selects(
	) | rpl::start_with_next([=](const CallbackData &data) {
		if (!data.selected) {
			return;
		}
		for (auto i = 0; i < _actionWidgets.size(); i++) {
			if (i != data.index) {
				_actionWidgets[i]->setSelected(false);
			}
		}
		if (_activatedCallback) {
			_activatedCallback(data);
		}
	}, widget->lifetime());

	widget->clicks(
	) | rpl::start_with_next([=](const CallbackData &data) {
		if (_triggeredCallback) {
			_triggeredCallback(data);
		}
	}, widget->lifetime());

	widget->minWidthValue(
	) | rpl::start_with_next([=] {
		const auto newWidth = _forceWidth
			? _forceWidth
			: _actionWidgets.empty()
			? _st.widthMin
			: (*ranges::max_element(
				_actionWidgets,
				std::less<>(),
				&ItemBase::minWidth))->minWidth();
		resizeFromInner(newWidth, height());
	}, widget->lifetime());

	_actionWidgets.push_back(std::move(widget));

	const auto newHeight = ranges::accumulate(
		_actionWidgets,
		0,
		ranges::plus(),
		&ItemBase::height);
	resizeFromInner(width(), newHeight);
	updateSelected(QCursor::pos());

	return action;
}

not_null<QAction*> Menu::addSeparator() {
	const auto separator = new QAction(this);
	separator->setSeparator(true);
	auto item = base::make_unique_q<Separator>(this, _st, separator);
	return addAction(std::move(item));
}

void Menu::clearActions() {
	_actionWidgets.clear();
	for (auto action : base::take(_actions)) {
		if (action->parent() == this) {
			delete action;
		}
	}
	resizeFromInner(_forceWidth ? _forceWidth : _st.widthMin, _st.skip * 2);
}

void Menu::finishAnimating() {
	for (const auto &widget : _actionWidgets) {
		widget->finishAnimating();
	}
}

bool Menu::empty() const {
	return _actionWidgets.empty();
}

void Menu::resizeFromInner(int w, int h) {
	if ((w == width()) && (h == height())) {
		return;
	}
	resize(w, h);
	_resizesFromInner.fire({});
}

rpl::producer<> Menu::resizesFromInner() const {
	return _resizesFromInner.events();
}

void Menu::setShowSource(TriggeredSource source) {
	const auto mouseSelection = (source == TriggeredSource::Mouse);
	setSelected(
		(mouseSelection || _actions.empty()) ? -1 : 0,
		mouseSelection);
}

const std::vector<not_null<QAction*>> &Menu::actions() const {
	return _actions;
}

void Menu::setForceWidth(int forceWidth) {
	_forceWidth = forceWidth;
	resizeFromInner(_forceWidth, height());
}

void Menu::updateSelected(QPoint globalPosition) {
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
		handleKeyPress(e);
	}
}

ItemBase *Menu::findSelectedAction() const {
	const auto it = ranges::find_if(_actionWidgets, &ItemBase::isSelected);
	return (it == end(_actionWidgets)) ? nullptr : it->get();
}

void Menu::handleKeyPress(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	const auto selected = findSelectedAction();
	if ((key != Qt::Key_Up && key != Qt::Key_Down) || _actions.empty()) {
		if (selected) {
			selected->handleKeyPress(e);
		}
		return;
	}

	const auto delta = (key == Qt::Key_Down ? 1 : -1);
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
		setSelected(newSelected, false);
	}
}

void Menu::clearSelection() {
	setSelected(-1, false);
}

void Menu::clearMouseSelection() {
	const auto selected = findSelectedAction();
	const auto mouseSelection = selected
		? (selected->lastTriggeredSource() == TriggeredSource::Mouse)
		: false;
	if (mouseSelection && !_childShown) {
		clearSelection();
	}
}

void Menu::setSelected(int selected, bool isMouseSelection) {
	if (selected >= _actionWidgets.size()) {
		selected = -1;
	}
	const auto source = isMouseSelection
		? TriggeredSource::Mouse
		: TriggeredSource::Keyboard;
	if (const auto selectedItem = findSelectedAction()) {
		if (selectedItem->index() == selected) {
			return;
		}
		selectedItem->setSelected(false, source);
	}
	if (selected >= 0) {
		_actionWidgets[selected]->setSelected(true, source);
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

} // namespace Ui::Menu
