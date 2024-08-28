// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast_manager.h"

#include "ui/toast/toast_widget.h"
#include "ui/qt_object_factory.h"

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
			if (i->first->parentWidget() == o) {
				i->first->parentResized();
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

base::weak_ptr<Instance> Manager::addToast(
		std::unique_ptr<Instance> &&toast) {
	_toasts.push_back(std::move(toast));
	const auto t = _toasts.back().get();
	const auto widget = t->_widget.get();

	_toastByWidget.emplace(widget, t);
	connect(widget, &QObject::destroyed, [=] {
		toastWidgetDestroyed(widget);
	});
	if (const auto parent = widget->parentWidget()) {
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
			_toastParents.push_back(parent);
			parent->installEventFilter(this);
		}
	}
	if (t->_hideAt > 0) {
		const auto nearestHide = _toastByHideTime.empty()
			? 0LL
			: _toastByHideTime.begin()->first;
		_toastByHideTime.emplace(t->_hideAt, t);
		if (!nearestHide || _toastByHideTime.begin()->first < nearestHide) {
			startNextHideTimer();
		}
	}
	return make_weak(t);
}

void Manager::hideByTimer() {
	auto now = crl::now();
	for (auto i = _toastByHideTime.begin(); i != _toastByHideTime.cend();) {
		if (i->first <= now) {
			const auto toast = i->second;
			i = _toastByHideTime.erase(i);
			toast->hideAnimated();
		} else {
			break;
		}
	}
	startNextHideTimer();
}

void Manager::toastWidgetDestroyed(QObject *widget) {
	const auto i = _toastByWidget.find(static_cast<Widget*>(widget));
	if (i == _toastByWidget.cend()) {
		return;
	}
	const auto toast = i->second;
	_toastByWidget.erase(i);
	toast->_widget.release();

	for (auto i = begin(_toastByHideTime); i != end(_toastByHideTime); ++i) {
		if (i->second == toast) {
			_toastByHideTime.erase(i);
			break;
		}
	}

	const auto j = ranges::find(
		_toasts,
		toast.get(),
		&std::unique_ptr<Instance>::get);
	if (j != end(_toasts)) {
		_toasts.erase(j);
	}
}

void Manager::startNextHideTimer() {
	if (_toastByHideTime.empty()) {
		return;
	}

	auto ms = crl::now();
	if (ms >= _toastByHideTime.begin()->first) {
		crl::on_main(this, [=] {
			hideByTimer();
		});
	} else {
		_hideTimer.callOnce(_toastByHideTime.begin()->first - ms);
	}
}

Manager::~Manager() {
	ManagersMap.remove(parent());
	_toastByWidget.clear();
	_toasts.clear();
}

} // namespace internal
} // namespace Toast
} // namespace Ui
