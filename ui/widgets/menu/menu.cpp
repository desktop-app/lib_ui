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
#include "ui/widgets/scroll_area.h"
#include "styles/style_widgets.h"

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
		QPainter(this).fillRect(clip, _st.itemBg);
	}, lifetime());

	positionValue(
	) | rpl::start_with_next([=] {
		handleMouseMove(QCursor::pos());
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
		std::unique_ptr<QMenu> submenu,
		const style::icon *icon,
		const style::icon *iconOver) {
	const auto action = new QAction(text, this);
	action->setMenu(submenu.release());
	return addAction(action, icon, iconOver);
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
	return insertAction(_actions.size(), std::move(widget));
}

not_null<QAction*> Menu::insertAction(
		int position,
		base::unique_qptr<ItemBase> widget) {
	Expects(position >= 0 && position <= _actions.size());
	Expects(position >= 0 && position <= _actionWidgets.size());

	const auto raw = widget.get();
	const auto action = raw->action();
	_actions.insert(begin(_actions) + position, action);

	raw->setParent(this);
	raw->show();
	raw->setIndex(position);
	for (auto i = position, to = int(_actionWidgets.size()); i != to; ++i) {
		_actionWidgets[i]->setIndex(i + 1);
	}
	_actionWidgets.insert(
		begin(_actionWidgets) + position,
		std::move(widget));

	raw->selects(
	) | rpl::start_with_next([=](const CallbackData &data) {
		if (!data.selected) {
			if (!findSelectedAction()
				&& data.index < _actionWidgets.size()
				&& _childShownAction == data.action) {
				const auto widget = _actionWidgets[data.index].get();
				widget->setSelected(true, widget->lastTriggeredSource());
			}
			return;
		}
		_lastSelectedByMouse = (data.source == TriggeredSource::Mouse);
		for (auto i = 0; i < _actionWidgets.size(); i++) {
			if (i != data.index) {
				_actionWidgets[i]->setSelected(false);
			}
		}
		if (_activatedCallback) {
			_activatedCallback(data);
		}
	}, raw->lifetime());

	raw->clicks(
	) | rpl::start_with_next([=](const CallbackData &data) {
		if (_triggeredCallback) {
			_triggeredCallback(data);
		}
	}, raw->lifetime());

	QObject::connect(action.get(), &QAction::changed, raw, [=] {
		// Select an item under mouse that was disabled and became enabled.
		if (_lastSelectedByMouse
			&& !findSelectedAction()
			&& action->isEnabled()) {
			updateSelected(QCursor::pos());
		}
	});

	const auto recountWidth = [=] {
		return _forceWidth
			? _forceWidth
			: std::clamp(
				(_actionWidgets.empty()
					? 0
					: (*ranges::max_element(
						_actionWidgets,
						std::less<>(),
						&ItemBase::minWidth))->minWidth()),
				_st.widthMin,
				_st.widthMax);
	};
	const auto recountHeight = [=] {
		auto result = 0;
		for (const auto &widget : _actionWidgets) {
			if (widget->y() != result) {
				widget->move(0, result);
			}
			result += widget->height();
		}
		return result;
	};

	raw->minWidthValue(
	) | rpl::skip(1) | rpl::filter([=] {
		return !_forceWidth;
	}) | rpl::start_with_next([=] {
		resizeFromInner(recountWidth(), height());
	}, raw->lifetime());

	raw->heightValue(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		resizeFromInner(width(), recountHeight());
	}, raw->lifetime());

	resizeFromInner(recountWidth(), recountHeight());

	updateSelected(QCursor::pos());

	return action;
}

not_null<QAction*> Menu::addSeparator(const style::MenuSeparator *st) {
	const auto separator = new QAction(this);
	separator->setSeparator(true);
	auto item = base::make_unique_q<Separator>(
		this,
		_st,
		st ? *st : _st.separator,
		separator);
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

void Menu::clearLastSeparator() {
	if (_actionWidgets.empty() || _actions.empty()) {
		return;
	}
	if (_actionWidgets.back()->action() == _actions.back()) {
		if (_actions.back()->isSeparator()) {
			resizeFromInner(
				width(),
				height() - _actionWidgets.back()->height());
			_actionWidgets.pop_back();
			if (_actions.back()->parent() == this) {
				delete _actions.back();
				_actions.pop_back();
			}
		}
	}
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
	if (const auto s = QSize(w, h); s != size()) {
		resize(s);
		_resizesFromInner.fire({});
	}
}

rpl::producer<> Menu::resizesFromInner() const {
	return _resizesFromInner.events();
}

rpl::producer<ScrollToRequest> Menu::scrollToRequests() const {
	return _scrollToRequests.events();
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
			_lastSelectedByMouse = true;

			// It may actually fail to become selected (if it is disabled).
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
	} while (newSelected != start
		&& (!_actionWidgets[newSelected]->isEnabled()));

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
	if (mouseSelection && !_childShownAction) {
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
	if (selected >= 0 && source == TriggeredSource::Keyboard) {
		const auto widget = _actionWidgets[selected].get();
		_scrollToRequests.fire({
			widget->y(),
			widget->y() + widget->height(),
		});
	}
	if (const auto selectedItem = findSelectedAction()) {
		if (selectedItem->index() == selected) {
			return;
		}
		selectedItem->setSelected(false, source);
	}
	if (selected >= 0) {
		_actionWidgets[selected].get()->setSelected(true, source);
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
	const auto margins = style::margins(0, _st.skip, 0, _st.skip);
	const auto inner = rect().marginsRemoved(margins);
	const auto localPosition = mapFromGlobal(globalPosition);
	const auto pressed = (inner.contains(localPosition)
		&& _lastSelectedByMouse)
		? findSelectedAction()
		: nullptr;
	if (pressed) {
		pressed->setClicked();
	} else {
		if (_mousePressDelegate) {
			_mousePressDelegate(globalPosition);
		}
	}
}

void Menu::handleMouseRelease(QPoint globalPosition) {
	if (!rect().contains(mapFromGlobal(globalPosition))
			&& _mouseReleaseDelegate) {
		_mouseReleaseDelegate(globalPosition);
	}
}

} // namespace Ui::Menu
