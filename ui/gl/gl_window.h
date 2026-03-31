// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Ui {
class RpWindow;
class RpWidget;
} // namespace Ui

namespace Ui::GL {

enum class Backend;
struct Capabilities;

class Window final {
public:
	enum class Translucent { Yes };

	Window();
	explicit Window(Translucent);
	explicit Window(Fn<Backend(Capabilities)> chooseBackend);
	Window(Fn<Backend(Capabilities)> chooseBackend, Translucent);
	~Window();

	[[nodiscard]] Backend backend() const;
	[[nodiscard]] not_null<RpWindow*> window() const;
	[[nodiscard]] not_null<RpWidget*> widget() const;

private:
	[[nodiscard]] std::unique_ptr<RpWindow> createWindow(
		const Fn<Backend(Capabilities)> &chooseBackend,
		bool translucent = false);

	Backend _backend = Backend();
	const std::unique_ptr<RpWindow> _window;

};

} // namespace Ui::GL
