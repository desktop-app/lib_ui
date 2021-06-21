// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once


namespace Ui {
class Window;
class RpWidget;
} // namespace Ui

namespace Ui::GL {

enum class Backend;

class Window final {
public:
	Window();
	~Window();

	[[nodiscard]] Backend backend() const;
	[[nodiscard]] not_null<Ui::Window*> window() const;
	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

private:
	[[nodiscard]] std::unique_ptr<Ui::Window> createWindow();
	[[nodiscard]] std::unique_ptr<Ui::RpWidget> createNativeBodyWrap();

	Ui::GL::Backend _backend = Ui::GL::Backend();
	const std::unique_ptr<Ui::Window> _window;
	const std::unique_ptr<Ui::RpWidget> _bodyNativeWrap;
	const not_null<Ui::RpWidget*> _body;

};

} // namespace Ui::GL
