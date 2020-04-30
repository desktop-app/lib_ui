// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/toast/toast.h"
#include "base/timer.h"

namespace Ui {
namespace Toast {
namespace internal {

class Widget;
class Manager final : public QObject {
	struct CreateTag {
	};

public:
	Manager(not_null<QWidget*> parent, const CreateTag &);
	Manager(const Manager &other) = delete;
	Manager &operator=(const Manager &other) = delete;
	~Manager();

	static not_null<Manager*> instance(not_null<QWidget*> parent);

	base::weak_ptr<Instance> addToast(std::unique_ptr<Instance> &&toast);

protected:
	bool eventFilter(QObject *o, QEvent *e);

private:
	void toastWidgetDestroyed(QObject *widget);
	void startNextHideTimer();
	void hideByTimer();

	base::Timer _hideTimer;

	base::flat_multi_map<crl::time, not_null<Instance*>> _toastByHideTime;
	base::flat_map<not_null<Widget*>, not_null<Instance*>> _toastByWidget;
	std::vector<std::unique_ptr<Instance>> _toasts;
	std::vector<QPointer<QWidget>> _toastParents;

};

} // namespace internal
} // namespace Toast
} // namespace Ui
