// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_windows_direct_manipulation.h"

#include "base/integration.h"
#include "base/platform/base_platform_info.h"
#include "ui/rp_widget.h"
#include "ui/platform/win/ui_window_win.h"

namespace Ui::Platform {

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
: _handle(GetWindowHandle(widget))
, _interacting([=] { _updateManager->Update(nullptr); }) {
	if (!init(widget)) {
		destroy();
	}
}

DirectManipulation::~DirectManipulation() {
	destroy();
}

bool DirectManipulation::valid() const {
	return _manager != nullptr;
}

bool DirectManipulation::init(not_null<RpWidget*> widget) {
	if (!_handle || !::Platform::IsWindows10OrGreater()) {
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
		_handle,
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
	}, _lifetime);

	widget->sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto r = QRect(QPoint(), size * widget->devicePixelRatio());
		_handler->setViewportSize(r.size());
		const auto rect = RECT{ r.left(), r.top(), r.right(), r.bottom() };
		_viewport->SetViewportRect(&rect);
	}, _lifetime);

	hr = _viewport->AddEventHandler(_handle, _handler.get(), &_cookie);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	RECT rect = { 0, 0, 1024, 1024 };
	hr = _viewport->SetViewportRect(&rect);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = _manager->Activate(_handle);
	if (!SUCCEEDED(hr)) {
		return false;
	}

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

auto DirectManipulation::events() const -> rpl::producer<Event> {
	if (!_handler) {
		return rpl::never<Event>();
	}
	return [events = _handler->events()](auto consumer) mutable {
		auto result = rpl::lifetime();
		std::move(
			events
		) | rpl::start_with_next([=](Event &&event) {
			base::Integration::Instance().enterFromEventLoop([&] {
				consumer.put_next(std::move(event));
			});
		}, result);
		return result;
	};
}

void DirectManipulation::handlePointerHitTest(WPARAM wParam) {
	const auto id = UINT32(GET_POINTERID_WPARAM(wParam));
	auto type = POINTER_INPUT_TYPE();
	if (::GetPointerType(id, &type) && type == PT_TOUCHPAD) {
		_viewport->SetContact(id);
	}
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
		_manager->Deactivate(_handle);
		_manager = nullptr;
	}
}

}  // namespace Ui::Platform
