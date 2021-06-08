// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/window.h"

#include "ui/platform/ui_platform_window.h"

namespace Ui {

Window::Window(QWidget *parent)
: RpWidget(parent)
, _helper(Platform::CreateWindowHelper(this)) {
	Expects(_helper != nullptr);

	hide();
}

Window::~Window() = default;

not_null<RpWidget*> Window::body() {
	return _helper->body();
}

not_null<const RpWidget*> Window::body() const {
	return _helper->body().get();
}

void Window::setTitle(const QString &title) {
	_helper->setTitle(title);
}

void Window::setTitleStyle(const style::WindowTitle &st) {
	_helper->setTitleStyle(st);
}

void Window::setMinimumSize(QSize size) {
	_helper->setMinimumSize(size);
}

void Window::setFixedSize(QSize size) {
	_helper->setFixedSize(size);
}

void Window::setStaysOnTop(bool enabled) {
	_helper->setStaysOnTop(enabled);
}

void Window::setGeometry(QRect rect) {
	_helper->setGeometry(rect);
}

void Window::showFullScreen() {
	_helper->showFullScreen();
}

void Window::showNormal() {
	_helper->showNormal();
}

void Window::close() {
	_helper->close();
}

void Window::setBodyTitleArea(
		Fn<WindowTitleHitTestFlags(QPoint)> testMethod) {
	_helper->setBodyTitleArea(std::move(testMethod));
}

} // namespace Ui
