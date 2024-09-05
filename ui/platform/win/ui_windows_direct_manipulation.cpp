// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_windows_direct_manipulation.h"

#include "base/integration.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/win/base_windows_safe_library.h"
#include "ui/platform/win/ui_window_win.h"
#include "ui/widgets/elastic_scroll.h" // kPixelToAngleDelta
#include "ui/rp_widget.h"

#include <qpa/qwindowsysteminterface.h>
#include <qpa/qwindowsysteminterface_p.h>

namespace Ui::Platform {
namespace {

[[nodiscard]] Qt::KeyboardModifiers LookupModifiers() {
	const auto check = [](int key) {
		return (GetKeyState(key) & 0x8000) != 0;
	};

	auto result = Qt::KeyboardModifiers();
	if (check(VK_SHIFT)) {
		result |= Qt::ShiftModifier;
	}
	// NB AltGr key (i.e., VK_RMENU on some keyboard layout) is not handled.
	if (check(VK_RMENU) || check(VK_MENU)) {
		result |= Qt::AltModifier;
	}
	if (check(VK_CONTROL)) {
		result |= Qt::ControlModifier;
	}
	if (check(VK_LWIN) || check(VK_RWIN)) {
		result |= Qt::MetaModifier;
	}
	return result;
}

UINT(__stdcall *GetDpiForWindow)(_In_ HWND hwnd);

[[nodiscard]] bool GetDpiForWindowSupported() {
	static const auto Result = [&] {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto user32 = base::Platform::SafeLoadLibrary(L"User32.dll");
		return LOAD_SYMBOL(user32, GetDpiForWindow);
#undef LOAD_SYMBOL
	}();
	return Result;
}

} // namespace

class DirectManipulation::Handler
	: public IDirectManipulationViewportEventHandler
	, public IDirectManipulationInteractionEventHandler {
public:
	Handler();

	void setViewportSize(QSize size);

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID iid,
		void **ppv) override;

	ULONG STDMETHODCALLTYPE AddRef() override {
		return ++_ref;
	}
	ULONG STDMETHODCALLTYPE Release() override {
		if (--_ref == 0) {
			delete this;
			return 0;
		}
		return _ref;
	}

	[[nodiscard]] rpl::producer<bool> interacting() const {
		return _interacting.value();
	}
	[[nodiscard]] rpl::producer<Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	~Handler();

	enum class State {
		None,
		Scroll,
		Fling,
		Pinch,
	};

	void transitionToState(State state);

	HRESULT STDMETHODCALLTYPE OnViewportStatusChanged(
		_In_ IDirectManipulationViewport* viewport,
		_In_ DIRECTMANIPULATION_STATUS current,
		_In_ DIRECTMANIPULATION_STATUS previous) override;

	HRESULT STDMETHODCALLTYPE OnViewportUpdated(
		_In_ IDirectManipulationViewport *viewport) override;

	HRESULT STDMETHODCALLTYPE OnContentUpdated(
		_In_ IDirectManipulationViewport *viewport,
		_In_ IDirectManipulationContent *content) override;

	HRESULT STDMETHODCALLTYPE OnInteraction(
		_In_ IDirectManipulationViewport2 *viewport,
		_In_ DIRECTMANIPULATION_INTERACTION_TYPE interaction) override;

	State _state = State::None;
	int _width = 0;
	int _height = 0;
	rpl::variable<bool> _interacting = false;
	rpl::event_stream<Event> _events;
	rpl::lifetime _lifetime;

	float _scale = 1.0f;
	float _xOffset = 0.f;
	float _yOffset = 0.f;
	bool _pendingScrollBegin = false;

	std::atomic<ULONG> _ref = 1;

};

DirectManipulation::Handler::Handler() {
}

DirectManipulation::Handler::~Handler() {
}

void DirectManipulation::Handler::setViewportSize(QSize size) {
	_width = size.width();
	_height = size.height();
}

STDMETHODIMP DirectManipulation::Handler::QueryInterface(
		REFIID iid,
		void **ppv) {
	if ((IID_IUnknown == iid) ||
		(IID_IDirectManipulationViewportEventHandler == iid)) {
		*ppv = static_cast<IDirectManipulationViewportEventHandler*>(this);
		AddRef();
		return S_OK;
	}
	if (IID_IDirectManipulationInteractionEventHandler == iid) {
		*ppv = static_cast<IDirectManipulationInteractionEventHandler*>(
			this);
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}

void DirectManipulation::Handler::transitionToState(State state)  {
	if (_state == state) {
		return;
	}

	const auto was = _state;
	_state = state;

	switch (was) {
	case State::Scroll: {
		if (state != State::Fling) {
			_events.fire({ .type = Event::Type::ScrollStop });
		}
	} break;
	case State::Fling: {
		_events.fire({ .type = Event::Type::FlingStop });
	} break;
	case State::Pinch: {
		// _events.fire({ .type = Event::Type::PinchStop });
	} break;
	}

	switch (state) {
	case State::Scroll: {
		_pendingScrollBegin = true;
	} break;
	case State::Fling: {
		Assert(was == State::Scroll);
		_events.fire({ .type = Event::Type::FlingStart });
	} break;
	case State::Pinch: {
		//_events.fire({ .type = Event::Type::PinchStart });
	} break;
	}
}

HRESULT DirectManipulation::Handler::OnViewportStatusChanged(
		IDirectManipulationViewport *viewport,
		DIRECTMANIPULATION_STATUS current,
		DIRECTMANIPULATION_STATUS previous) {
	Expects(viewport != nullptr);

	if (current == previous) {
		return S_OK;
	} else if (current == DIRECTMANIPULATION_INERTIA) {
		if (previous != DIRECTMANIPULATION_RUNNING
			|| _state != State::Scroll) {
			return S_OK;
		}
		transitionToState(State::Fling);
	}

	if (current == DIRECTMANIPULATION_RUNNING) {
		if (previous == DIRECTMANIPULATION_INERTIA) {
			transitionToState(State::None);
		}
	}

	if (current != DIRECTMANIPULATION_READY) {
		return S_OK;
	}

	if (_scale != 1.0f || _xOffset != 0. || _yOffset != 0.) {
		const auto hr = viewport->ZoomToRect(0, 0, _width, _height, FALSE);
		if (!SUCCEEDED(hr)) {
			return hr;
		}
	}

	_scale = 1.0f;
	_xOffset = 0.0f;
	_yOffset = 0.0f;

	transitionToState(State::None);

	return S_OK;
}

HRESULT DirectManipulation::Handler::OnViewportUpdated(
		IDirectManipulationViewport *viewport) {
	return S_OK;
}

HRESULT DirectManipulation::Handler::OnContentUpdated(
		IDirectManipulationViewport *viewport,
		IDirectManipulationContent *content) {
	Expects(viewport != nullptr);
	Expects(content != nullptr);

	float xform[6];
	const auto hr = content->GetContentTransform(xform, ARRAYSIZE(xform));
	if (!SUCCEEDED(hr)) {
		return hr;
	}

	float scale = xform[0];
	float xOffset = xform[4];
	float yOffset = xform[5];

	if (scale == 0.0f) {
		return hr;
	} else if (qFuzzyCompare(scale, _scale)
		&& xOffset == _xOffset
		&& yOffset == _yOffset) {
		return hr;
	}
	if (qFuzzyCompare(scale, 1.0f)) {
		if (_state == State::None) {
			transitionToState(State::Scroll);
		}
	} else {
		transitionToState(State::Pinch);
	}

	auto getIntDeltaPart = [](float &was, float now) {
		if (was < now) {
			const auto result = std::floor(now - was);
			was += result;
			return int(result);
		} else {
			const auto result = std::floor(was - now);
			was -= result;
			return -int(result);
		}
	};
	const auto d = QPoint(
		getIntDeltaPart(_xOffset, xOffset),
		getIntDeltaPart(_yOffset, yOffset));
	if ((_state == State::Scroll || _state == State::Fling) && d.isNull()) {
		return S_OK;
	}
	if (_state == State::Scroll) {
		if (_pendingScrollBegin) {
			_events.fire({ .type = Event::Type::ScrollStart, .delta = d });
			_pendingScrollBegin = false;
		} else {
			_events.fire({ .type = Event::Type::Scroll, .delta = d });
		}
	} else if (_state == State::Fling) {
		_events.fire({ .type = Event::Type::Fling, .delta = d });
	} else {
		//_events.fire({ .type = Event::Type::Pinch, .delta = ... });
	}
	_scale = scale;

	return hr;
}

HRESULT DirectManipulation::Handler::OnInteraction(
		IDirectManipulationViewport2 *viewport,
		DIRECTMANIPULATION_INTERACTION_TYPE interaction) {
	if (interaction == DIRECTMANIPULATION_INTERACTION_BEGIN) {
		_interacting = true;
	} else if (interaction == DIRECTMANIPULATION_INTERACTION_END) {
		_interacting = false;
	}
	return S_OK;
}

DirectManipulation::DirectManipulation(not_null<RpWidget*> widget)
: NativeEventFilter(widget)
, _interacting([=] { _updateManager->Update(nullptr); }) {
	widget->sizeValue() | rpl::start_with_next([=](QSize size) {
		sizeUpdated(size * widget->devicePixelRatio());
	}, _lifetime);

	widget->winIdValue() | rpl::start_with_next([=](WId winId) {
		destroy();

		if (const auto hwnd = reinterpret_cast<HWND>(winId)) {
			if (init(hwnd)) {
				sizeUpdated(widget->size() * widget->devicePixelRatio());
			} else {
				destroy();
			}
		}
	}, _lifetime);
}

DirectManipulation::~DirectManipulation() {
	destroy();
}

void DirectManipulation::sizeUpdated(QSize nativeSize) {
	if (const auto handler = _handler.get()) {
		const auto r = QRect(QPoint(), nativeSize);
		handler->setViewportSize(r.size());
		if (const auto viewport = _viewport.get()) {
			const auto rect = RECT{ r.x(), r.y(), r.right(), r.bottom() };
			viewport->SetViewportRect(&rect);
		}
	}
}

bool DirectManipulation::init(HWND hwnd) {
	if (!hwnd || !::Platform::IsWindows10OrGreater()) {
		return false;
	}
	_manager = base::WinRT::TryCreateInstance<IDirectManipulationManager>(
		CLSID_DirectManipulationManager);
	if (!_manager) {
		return false;
	}

	auto hr = S_OK;
	hr = _manager->GetUpdateManager(IID_PPV_ARGS(_updateManager.put()));
	if (!SUCCEEDED(hr) || !_updateManager) {
		return false;
	}

	hr = _manager->CreateViewport(
		nullptr,
		hwnd,
		IID_PPV_ARGS(_viewport.put()));
	if (!SUCCEEDED(hr) || !_viewport) {
		return false;
	}

	const auto configuration = DIRECTMANIPULATION_CONFIGURATION_INTERACTION
		| DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X
		| DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y
		| DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA
		| DIRECTMANIPULATION_CONFIGURATION_RAILS_X
		| DIRECTMANIPULATION_CONFIGURATION_RAILS_Y;

	hr = _viewport->ActivateConfiguration(configuration);
	if (!SUCCEEDED(hr)) {
		return false;
	}
	hr = _viewport->SetViewportOptions(
		DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	_handler.attach(new Handler());
	_handler->interacting(
	) | rpl::start_with_next([=](bool interacting) {
		base::Integration::Instance().enterFromEventLoop([&] {
			if (interacting) {
				_interacting.start();
			} else {
				_interacting.stop();
			}
		});
	}, _handler->lifetime());
	_handler->events() | rpl::start_with_next([=](Event &&event) {
		base::Integration::Instance().enterFromEventLoop([&] {
			_events.fire(std::move(event));
		});
	}, _handler->lifetime());

	hr = _viewport->AddEventHandler(hwnd, _handler.get(), &_cookie);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	RECT rect = { 0, 0, 1024, 1024 };
	hr = _viewport->SetViewportRect(&rect);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = _manager->Activate(hwnd);
	if (!SUCCEEDED(hr)) {
		return false;
	}
	_managerHandle = hwnd;

	hr = _viewport->Enable();
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = _updateManager->Update(nullptr);
	if (!SUCCEEDED(hr)) {
		return false;
	}
	return true;
}

bool DirectManipulation::filterNativeEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result) {
	switch (msg) {

	case DM_POINTERHITTEST:
		if (_viewport) {
			const auto id = UINT32(GET_POINTERID_WPARAM(wParam));
			auto type = POINTER_INPUT_TYPE();
			if (::GetPointerType(id, &type) && type == PT_TOUCHPAD) {
				_viewport->SetContact(id);
			}
			return true;
		}
		break;

	}
	return false;
}

auto DirectManipulation::events() const -> rpl::producer<Event> {
	return _events.events();
}

void DirectManipulation::destroy() {
	_interacting.stop();

	if (_handler) {
		_handler = nullptr;
	}

	if (_viewport) {
		_viewport->Stop();
		if (_cookie) {
			_viewport->RemoveEventHandler(_cookie);
			_cookie = 0;
		}
		_viewport->Abandon();
		_viewport = nullptr;
	}

	if (_updateManager) {
		_updateManager = nullptr;
	}

	if (_manager) {
		if (_managerHandle) {
			_manager->Deactivate(_managerHandle);
		}
		_manager = nullptr;
	}
}

void ActivateDirectManipulation(not_null<RpWidget*> window) {
	auto dm = std::make_unique<DirectManipulation>(window);

	dm->events(
	) | rpl::start_with_next([=](const DirectManipulationEvent &event) {
		using Type = DirectManipulationEventType;
		const auto send = [&](Qt::ScrollPhase phase) {
			const auto windowHandle = window->windowHandle();
			const auto hwnd = reinterpret_cast<HWND>(window->winId());
			if (!windowHandle || !hwnd) {
				return;
			}
			auto global = POINT();
			::GetCursorPos(&global);
			auto local = global;
			::ScreenToClient(hwnd, &local);
			const auto dpi = GetDpiForWindowSupported()
				? GetDpiForWindow(hwnd)
				: 0;
			const auto scale = dpi ? (96. / dpi) : 1.;
			const auto delta = QPointF(event.delta) * scale;
			const auto inverted = true;
			QWindowSystemInterface::handleWheelEvent(
				windowHandle,
				QWindowSystemInterfacePrivate::eventTime.elapsed(),
				QPointF(local.x, local.y),
				QPointF(global.x, global.y),
				delta.toPoint(),
				(delta * kPixelToAngleDelta).toPoint(),
				LookupModifiers(),
				phase,
				Qt::MouseEventSynthesizedBySystem,
				inverted);
		};
		switch (event.type) {
		case Type::ScrollStart: send(Qt::ScrollBegin); break;
		case Type::Scroll: send(Qt::ScrollUpdate); break;
		case Type::FlingStart:
		case Type::Fling: send(Qt::ScrollMomentum); break;
		case Type::ScrollStop: send(Qt::ScrollEnd); break;
		case Type::FlingStop: send(Qt::ScrollEnd); break;
		}
	}, window->lifetime());

	window->lifetime().add([owned = std::move(dm)] {});
}

}  // namespace Ui::Platform
