// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animations.h"

namespace Ui {

class RpWidget;
class VerticalLayout;

class VerticalLayoutReorder final {
public:
	enum class State : uchar {
		Started,
		Applied,
		Cancelled,
	};
	struct Single {
		not_null<RpWidget*> widget;
		int oldPosition = 0;
		int newPosition = 0;
		State state = State::Started;
	};

	VerticalLayoutReorder(not_null<VerticalLayout*> layout);

	void start();
	void cancel();
	[[nodiscard]] rpl::producer<Single> updates() const;

private:
	struct Entry {
		not_null<RpWidget*> widget;
		Ui::Animations::Simple shiftAnimation;
		int shift = 0;
		int finalShift = 0;
		int deltaShift = 0;
	};

	void mouseMove(not_null<RpWidget*> widget, QPoint position);
	void mousePress(
		not_null<RpWidget*> widget,
		Qt::MouseButton button,
		QPoint position);
	void mouseRelease(not_null<RpWidget*> widget, Qt::MouseButton button);

	void checkForStart(QPoint position);
	void updateOrder(int index, QPoint position);
	void cancelCurrent();
	void finishCurrent();
	void cancelCurrent(int index);

	[[nodiscard]] int indexOf(not_null<RpWidget*> widget) const;
	void moveToShift(int index, int shift);
	void updateShift(not_null<RpWidget*> widget, int indexHint);

	const not_null<Ui::VerticalLayout*> _layout;

	RpWidget *_currentWidget = nullptr;
	int _currentStart = 0;
	int _currentDesiredIndex = 0;
	State _currentState = State::Cancelled;
	std::vector<Entry> _entries;
	rpl::event_stream<Single> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Ui
