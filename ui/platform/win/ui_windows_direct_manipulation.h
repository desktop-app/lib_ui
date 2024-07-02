// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/platform/win/base_windows_winrt.h"
#include "ui/platform/win/ui_windows_native_event_filter.h"
#include "ui/effects/animations.h"

#include <QtCore/QPoint>

#include <windows.h>
#include <directmanipulation.h>

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Platform {

enum class DirectManipulationEventType {
	ScrollStart,
	Scroll,
	ScrollStop,
	FlingStart,
	Fling,
	FlingStop,
};

struct DirectManipulationEvent {
	using Type = DirectManipulationEventType;
	Type type = Type ();
	QPoint delta;
};

class DirectManipulation final : public NativeEventFilter {
public:
	explicit DirectManipulation(not_null<RpWidget*> widget);
	~DirectManipulation();

	using Event = DirectManipulationEvent;
	[[nodiscard]] rpl::producer<Event> events() const;

private:
	class Handler;

	bool init(HWND hwnd);
	void sizeUpdated(QSize nativeSize);
	void destroy();

	bool filterNativeEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) override;

	winrt::com_ptr<IDirectManipulationManager> _manager;
	winrt::com_ptr<IDirectManipulationUpdateManager> _updateManager;
	winrt::com_ptr<IDirectManipulationViewport> _viewport;
	winrt::com_ptr<Handler> _handler;
	HWND _managerHandle = nullptr;
	DWORD _cookie = 0;
	//bool has_animation_observer_ = false;

	Ui::Animations::Basic _interacting;
	rpl::event_stream<Event> _events;
	rpl::lifetime _lifetime;

};

void ActivateDirectManipulation(not_null<RpWidget*> window);

} // namespace Ui::Platform
