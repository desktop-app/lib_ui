// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_window.h"

#include "ui/gl/gl_detection.h"
#include "ui/widgets/rp_window.h"
#include "base/debug_log.h"

namespace Ui::GL {
namespace {

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
: _window(createWindow(ChooseBackendWrap(chooseBackend))) {
}

Window::~Window() = default;

Backend Window::backend() const {
	return _backend;
}

not_null<RpWindow*> Window::window() const {
	return _window.get();
}

not_null<RpWidget*> Window::widget() const {
	return _window->body().get();
}

std::unique_ptr<RpWindow> Window::createWindow(
		const Fn<Backend(Capabilities)> &chooseBackend) {
	_backend = chooseBackend(CheckCapabilities());
	return std::make_unique<RpWindow>();
}

} // namespace Ui::GL
