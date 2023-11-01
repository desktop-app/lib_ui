// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_window.h"

#include "ui/gl/gl_detection.h"
#include "ui/widgets/rp_window.h"
#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "base/debug_log.h"

#ifdef Q_OS_WIN
#include "ui/platform/win/ui_window_win.h"
#endif // Q_OS_WIN

#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Ui::GL {
namespace {

constexpr auto kUseNativeChild = false;// ::Platform::IsWindows();

class RpWidgetOpenGL : public RpWidget {
protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
	std::optional<QPlatformBackingStoreRhiConfig> rhiConfig() const override {
		return { QPlatformBackingStoreRhiConfig::OpenGL };
	}
#endif // Qt >= 6.4.0
};

class RpWindowOpenGL : public RpWindow {
protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
	std::optional<QPlatformBackingStoreRhiConfig> rhiConfig() const override {
		return { QPlatformBackingStoreRhiConfig::OpenGL };
	}
#endif // Qt >= 6.4.0
};

[[nodiscard]] Fn<Backend(Capabilities)> ChooseBackendWrap(
		Fn<Backend(Capabilities)> chooseBackend) {
	return [=](Capabilities capabilities) {
		const auto backend = chooseBackend(capabilities);
		const auto use = backend == Backend::OpenGL;
		LOG(("OpenGL: %1 (Window)").arg(use ? "[TRUE]" : "[FALSE]"));
		return backend;
	};
}

} // namespace

Window::Window() : Window(ChooseBackendDefault) {
}

Window::Window(Fn<Backend(Capabilities)> chooseBackend)
: _window(createWindow(ChooseBackendWrap(chooseBackend)))
, _bodyNativeWrap(createNativeBodyWrap(ChooseBackendWrap(chooseBackend)))
, _body(_bodyNativeWrap ? _bodyNativeWrap.get() : _window->body().get()) {
}

Window::~Window() = default;

Backend Window::backend() const {
	return _backend;
}

not_null<RpWindow*> Window::window() const {
	return _window.get();
}

not_null<RpWidget*> Window::widget() const {
	return _body.get();
}

std::unique_ptr<RpWindow> Window::createWindow(
		const Fn<Backend(Capabilities)> &chooseBackend) {
	std::unique_ptr<RpWindow> result = std::make_unique<RpWindowOpenGL>();
	if constexpr (!kUseNativeChild) {
		_backend = chooseBackend(CheckCapabilities(result.get()));
		if (_backend != Backend::OpenGL) {
			// We have to create a new window, if OpenGL initialization failed.
			result = std::make_unique<RpWindow>();
		}
	}
	return result;
}

std::unique_ptr<RpWidget> Window::createNativeBodyWrap(
		const Fn<Backend(Capabilities)> &chooseBackend) {
	if constexpr (!kUseNativeChild) {
		return nullptr;
	}
	const auto create = [] {
		auto result = std::make_unique<RpWidgetOpenGL>();
		result->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
		result->setAttribute(Qt::WA_NativeWindow);
		result->setAttribute(Qt::WA_DontCreateNativeAncestors);
		result->setAttribute(Qt::WA_OpaquePaintEvent);
		result->setAttribute(Qt::WA_NoSystemBackground);
		return result;
	};

	auto result = create();
	_backend = chooseBackend(CheckCapabilities(result.get()));
	if (_backend != Backend::OpenGL) {
		// We have to create a new window, if OpenGL initialization failed.
		result = create();
	}

	const auto nativeParent = _window->body();
	nativeParent->setAttribute(Qt::WA_OpaquePaintEvent);
	nativeParent->setAttribute(Qt::WA_NoSystemBackground);

	const auto raw = result.get();
	raw->setParent(nativeParent);
	raw->show();
	raw->update();

#ifdef Q_OS_WIN
	// In case a child native window fully covers the parent window,
	// the system never sends a WM_PAINT message to the parent window.
	//
	// In this case if you minimize / hide the parent window, it receives
	// QExposeEvent with isExposed() == false in window state change handler.
	//
	// But it never receives QExposeEvent with isExposed() == true, because
	// window state change handler doesn't send it, instead the WM_PAINT is
	// supposed to send it. No WM_PAINT -> no expose -> broken UI updating.
	const auto childWindow = raw->windowHandle();
	base::install_event_filter(childWindow, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Expose && childWindow->isExposed()) {
			Platform::SendWMPaintForce(_window.get());
		}
		return base::EventFilterResult::Continue;
	});

	_window->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		auto geometry = QRect(QPoint(), size);
		if constexpr (::Platform::IsWindows()) {
			if (const auto screen = _window->screen()) {
				if (screen->size() == size) {
					// Fix flicker in FullScreen OpenGL window on Windows.
					geometry = geometry.marginsAdded({ 0, 0, 0, 1 });
				}
			}
		}
		raw->setGeometry(geometry);
	}, raw->lifetime());
#endif // Q_OS_WIN

	return result;
}

} // namespace Ui::GL
