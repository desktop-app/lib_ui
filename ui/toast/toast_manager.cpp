// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast_manager.h"

#include "ui/toast/toast_widget.h"

namespace Ui {
namespace Toast {
namespace internal {
namespace {

base::flat_map<not_null<QObject*>, not_null<Manager*>> ManagersMap;

} // namespace

Manager::Manager(not_null<QWidget*> parent, const CreateTag &)
: QObject(parent)
, _hideTimer([=] { hideByTimer(); }) {
}

bool Manager::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Resize) {
		for (auto i = _toastByWidget.cbegin(), e = _toastByWidget.cend(); i != e; ++i) {
			if (i.key()->parentWidget() == o) {
				i.key()->onParentResized();
			}
		}
	}
	return QObject::eventFilter(o, e);
}

not_null<Manager*> Manager::instance(not_null<QWidget*> parent) {
	auto i = ManagersMap.find(parent);
	if (i == end(ManagersMap)) {
		i = ManagersMap.emplace(
			parent,
			Ui::CreateChild<Manager>(parent.get(), CreateTag())
		).first;
	}
	return i->second;
}

void Manager::addToast(std::unique_ptr<Instance> &&toast) {
	_toasts.push_back(toast.release());
	Instance *t = _toasts.back();
	Widget *widget = t->_widget.get();

	_toastByWidget.insert(widget, t);
	connect(widget, SIGNAL(destroyed(QObject*)), this, SLOT(onToastWidgetDestroyed(QObject*)));
	if (auto parent = widget->parentWidget()) {
		auto found = false;
		for (auto i = _toastParents.begin(); i != _toastParents.cend();) {
			if (*i == parent) {
				found = true;
				break;
			} else if (!*i) {
				i = _toastParents.erase(i);
			} else {
				++i;
			}
		}
		if (!found) {
			_toastParents.insert(parent);
			parent->installEventFilter(this);
		}
	}

	auto oldHideNearestMs = _toastByHideTime.isEmpty() ? 0LL : _toastByHideTime.firstKey();
	_toastByHideTime.insert(t->_hideAtMs, t);
	if (!oldHideNearestMs || _toastByHideTime.firstKey() < oldHideNearestMs) {
		startNextHideTimer();
	}
}

void Manager::hideByTimer() {
	auto now = crl::now();
	for (auto i = _toastByHideTime.begin(); i != _toastByHideTime.cend();) {
		if (i.key() <= now) {
			auto toast = i.value();
			i = _toastByHideTime.erase(i);
			toast->hideAnimated();
		} else {
			break;
		}
	}
	startNextHideTimer();
}

void Manager::onToastWidgetDestroyed(QObject *widget) {
	auto i = _toastByWidget.find(static_cast<Widget*>(widget));
	if (i != _toastByWidget.cend()) {
		auto toast = i.value();
		_toastByWidget.erase(i);
		toast->_widget.release();

		int index = _toasts.indexOf(toast);
		if (index >= 0) {
			_toasts.removeAt(index);
			delete toast;
		}
	}
}

void Manager::startNextHideTimer() {
	if (_toastByHideTime.isEmpty()) return;

	auto ms = crl::now();
	if (ms >= _toastByHideTime.firstKey()) {
		crl::on_main(this, [=] {
			hideByTimer();
		});
	} else {
		_hideTimer.callOnce(_toastByHideTime.firstKey() - ms);
	}
}

Manager::~Manager() {
	ManagersMap.remove(parent());
}

} // namespace internal
} // namespace Toast
} // namespace Ui
