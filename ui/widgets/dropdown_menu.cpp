// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/dropdown_menu.h"

#include <QtGui/QtEvents>

namespace Ui {

DropdownMenu::DropdownMenu(QWidget *parent, const style::DropdownMenu &st) : InnerDropdown(parent, st.wrap)
, _st(st) {
	_menu = setOwnedWidget(object_ptr<Menu::Menu>(this, _st.menu));
	init();
}

// Not ready with submenus yet.
//DropdownMenu::DropdownMenu(QWidget *parent, QMenu *menu, const style::DropdownMenu &st) : InnerDropdown(parent, st.wrap)
//, _st(st) {
//	_menu = setOwnedWidget(object_ptr<Menu>(this, menu, _st.menu));
//	init();
//
//	for (auto action : actions()) {
//		if (auto submenu = action->menu()) {
//			auto it = _submenus.insert(action, new DropdownMenu(submenu, st));
//			it.value()->deleteOnHide(false);
//		}
//	}
//}

void DropdownMenu::init() {
	InnerDropdown::setHiddenCallback([this] { hideFinish(); });

	_menu->resizesFromInner(
	) | rpl::start_with_next([=] {
		resizeToContent();
	}, _menu->lifetime());
	_menu->setActivatedCallback([this](const Menu::CallbackData &data) {
		handleActivated(data);
	});
	_menu->setTriggeredCallback([this](const Menu::CallbackData &data) {
		handleTriggered(data);
	});
	_menu->setKeyPressDelegate([this](int key) { return handleKeyPress(key); });
	_menu->setMouseMoveDelegate([this](QPoint globalPosition) { handleMouseMove(globalPosition); });
	_menu->setMousePressDelegate([this](QPoint globalPosition) { handleMousePress(globalPosition); });
	_menu->setMouseReleaseDelegate([this](QPoint globalPosition) { handleMouseRelease(globalPosition); });

	setMouseTracking(true);

	hide();
}

not_null<QAction*> DropdownMenu::addAction(
		base::unique_qptr<Menu::ItemBase> widget) {
	return _menu->addAction(std::move(widget));
}

not_null<QAction*> DropdownMenu::addAction(const QString &text, Fn<void()> callback, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, std::move(callback), icon, iconOver);
}

not_null<QAction*> DropdownMenu::addSeparator() {
	return _menu->addSeparator();
}

void DropdownMenu::clearActions() {
	//for (auto submenu : base::take(_submenus)) {
	//	delete submenu;
	//}
	return _menu->clearActions();
}

const std::vector<not_null<QAction*>> &DropdownMenu::actions() const {
	return _menu->actions();
}

bool DropdownMenu::empty() const {
	return _menu->empty();
}

void DropdownMenu::handleActivated(const Menu::CallbackData &data) {
	if (data.source == TriggeredSource::Mouse) {
		if (!popupSubmenuFromAction(data)) {
			if (auto currentSubmenu = base::take(_activeSubmenu)) {
				currentSubmenu->hideMenu(true);
			}
		}
	}
}

void DropdownMenu::handleTriggered(const Menu::CallbackData &data) {
	if (!popupSubmenuFromAction(data)) {
		hideMenu();
		_triggering = true;
		data.action->trigger();
		_triggering = false;
		if (_deleteLater) {
			_deleteLater = false;
			deleteLater();
		}
	}
}

// Not ready with submenus yet.
bool DropdownMenu::popupSubmenuFromAction(const Menu::CallbackData &data) {
	//if (auto submenu = _submenus.value(action)) {
	//	if (_activeSubmenu == submenu) {
	//		submenu->hideMenu(true);
	//	} else {
	//		popupSubmenu(submenu, actionTop, source);
	//	}
	//	return true;
	//}
	return false;
}

//void DropdownMenu::popupSubmenu(SubmenuPointer submenu, int actionTop, TriggeredSource source) {
//	if (auto currentSubmenu = base::take(_activeSubmenu)) {
//		currentSubmenu->hideMenu(true);
//	}
//	if (submenu) {
//		auto menuTopLeft = mapFromGlobal(_menu->mapToGlobal(QPoint(0, 0)));
//		auto menuBottomRight = mapFromGlobal(_menu->mapToGlobal(QPoint(_menu->width(), _menu->height())));
//		QPoint p(menuTopLeft.x() + (rtl() ? (width() - menuBottomRight.x()) : menuBottomRight.x()), menuTopLeft.y() + actionTop);
//		_activeSubmenu = submenu;
//		_activeSubmenu->showMenu(geometry().topLeft() + p, this, source);
//
//		_menu->setChildShown(true);
//	} else {
//		_menu->setChildShown(false);
//	}
//}

void DropdownMenu::forwardKeyPress(not_null<QKeyEvent*> e) {
	if (!handleKeyPress(e->key())) {
		_menu->handleKeyPress(e);
	}
}

bool DropdownMenu::handleKeyPress(int key) {
	if (_activeSubmenu) {
		_activeSubmenu->handleKeyPress(key);
		return true;
	} else if (key == Qt::Key_Escape) {
		hideMenu(_parent ? true : false);
		return true;
	} else if (key == (style::RightToLeft() ? Qt::Key_Right : Qt::Key_Left)) {
		if (_parent) {
			hideMenu(true);
			return true;
		}
	}
	return false;
}

void DropdownMenu::handleMouseMove(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseMove(globalPosition);
	}
}

void DropdownMenu::handleMousePress(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMousePress(globalPosition);
	} else {
		hideMenu();
	}
}

void DropdownMenu::handleMouseRelease(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseRelease(globalPosition);
	} else {
		hideMenu();
	}
}

void DropdownMenu::focusOutEvent(QFocusEvent *e) {
	hideMenu();
}

void DropdownMenu::hideEvent(QHideEvent *e) {
	if (_deleteOnHide) {
		if (_triggering) {
			_deleteLater = true;
		} else {
			deleteLater();
		}
	}
}

void DropdownMenu::keyPressEvent(QKeyEvent *e) {
	forwardKeyPress(e);
}

void DropdownMenu::mouseMoveEvent(QMouseEvent *e) {
	forwardMouseMove(e->globalPos());
}

void DropdownMenu::mousePressEvent(QMouseEvent *e) {
	forwardMousePress(e->globalPos());
}

void DropdownMenu::hideMenu(bool fast) {
	if (isHidden()) return;
	if (_parent && !isHiding()) {
		_parent->childHiding(this);
	}
	if (fast) {
		hideFast();
	} else {
		hideAnimated();
		if (_parent) {
			_parent->hideMenu();
		}
	}
	if (_activeSubmenu) {
		_activeSubmenu->hideMenu(fast);
	}
}

void DropdownMenu::childHiding(DropdownMenu *child) {
	if (_activeSubmenu && _activeSubmenu == child) {
		_activeSubmenu = SubmenuPointer();
	}
}

void DropdownMenu::hideFinish() {
	_menu->clearSelection();
	if (const auto onstack = _hiddenCallback) {
		onstack();
	}
}

// Not ready with submenus yet.
//void DropdownMenu::deleteOnHide(bool del) {
//	_deleteOnHide = del;
//}

//void DropdownMenu::popup(const QPoint &p) {
//	showMenu(p, nullptr, TriggeredSource::Mouse);
//}
//
//void DropdownMenu::showMenu(const QPoint &p, DropdownMenu *parent, TriggeredSource source) {
//	_parent = parent;
//
//	auto menuTopLeft = mapFromGlobal(_menu->mapToGlobal(QPoint(0, 0)));
//	auto w = p - QPoint(0, menuTopLeft.y());
//	auto r = QApplication::desktop()->screenGeometry(p);
//	if (rtl()) {
//		if (w.x() - width() < r.x() - _padding.left()) {
//			if (_parent && w.x() + _parent->width() - _padding.left() - _padding.right() + width() - _padding.right() <= r.x() + r.width()) {
//				w.setX(w.x() + _parent->width() - _padding.left() - _padding.right());
//			} else {
//				w.setX(r.x() - _padding.left());
//			}
//		} else {
//			w.setX(w.x() - width());
//		}
//	} else {
//		if (w.x() + width() - _padding.right() > r.x() + r.width()) {
//			if (_parent && w.x() - _parent->width() + _padding.left() + _padding.right() - width() + _padding.right() >= r.x() - _padding.left()) {
//				w.setX(w.x() + _padding.left() + _padding.right() - _parent->width() - width() + _padding.left() + _padding.right());
//			} else {
//				w.setX(r.x() + r.width() - width() + _padding.right());
//			}
//		}
//	}
//	if (w.y() + height() - _padding.bottom() > r.y() + r.height()) {
//		if (_parent) {
//			w.setY(r.y() + r.height() - height() + _padding.bottom());
//		} else {
//			w.setY(p.y() - height() + _padding.bottom());
//		}
//	}
//	if (w.y() < r.y()) {
//		w.setY(r.y());
//	}
//	move(w);
//
//	_menu->setShowSource(source);
//}

DropdownMenu::~DropdownMenu() {
	clearActions();
}

} // namespace Ui
