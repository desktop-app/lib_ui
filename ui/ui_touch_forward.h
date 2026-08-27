// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"

#include <QtCore/QPointer>

class QTouchEvent;
class QWidget;

namespace Ui {

// While a popup is up, Qt hands the touches to nobody and lets mouse events be
// synthesized from them instead - QWidgetWindow::handleTouchEvent() ignores
// them the moment QApplication::activePopupWidget() answers. A popup that wants
// the touches themselves takes them from its own window and gives them to the
// widgets under them, which is what Qt does for an ordinary window.
class TouchForward final {
public:
	// Answers whether a widget took the touch, because the mouse events are
	// synthesized exactly while nobody does.
	bool handle(not_null<QWidget*> window, not_null<QTouchEvent*> event);

private:
	// Only the beginning of a touch is offered to the widgets under it; the
	// updates and the end of it belong to the widget that took the beginning,
	// so that widget is remembered for every touch that is still down.
	base::flat_map<int, QPointer<QWidget>> _taken;

};

} // namespace Ui
