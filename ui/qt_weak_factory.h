// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtWidgets/QWidget>

namespace Ui {

template <typename Widget>
QPointer<Widget> MakeWeak(Widget *object) {
	return QPointer<Widget>(object);
}

template <typename Widget>
QPointer<const Widget> MakeWeak(const Widget *object) {
	return QPointer<const Widget>(object);
}

template <typename Widget>
QPointer<Widget> MakeWeak(gsl::not_null<Widget*> object) {
	return QPointer<Widget>(object.get());
}

template <typename Widget>
QPointer<const Widget> MakeWeak(gsl::not_null<const Widget*> object) {
	return QPointer<const Widget>(object.get());
}

} // namespace Ui
