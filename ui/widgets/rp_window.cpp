// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/rp_window.h"

#include "ui/platform/ui_platform_window.h"

namespace Ui {

RpWindow::RpWindow(QWidget *parent)
: RpWidget(parent)
, _helper(Platform::CreateWindowHelper(this)) {
	Expects(_helper != nullptr);

	hide();
}

RpWindow::~RpWindow() = default;

not_null<RpWidget*> RpWindow::body() {
	return _helper->body();
}

not_null<const RpWidget*> RpWindow::body() const {
	return _helper->body().get();
}

void RpWindow::setTitle(const QString &title) {
	_helper->setTitle(title);
}

void RpWindow::setTitleStyle(const style::WindowTitle &st) {
	_helper->setTitleStyle(st);
}

void RpWindow::setNativeFrame(bool enabled) {
	_helper->setNativeFrame(enabled);
}

void RpWindow::setMinimumSize(QSize size) {
	_helper->setMinimumSize(size);
}

void RpWindow::setFixedSize(QSize size) {
	_helper->setFixedSize(size);
}

void RpWindow::setStaysOnTop(bool enabled) {
	_helper->setStaysOnTop(enabled);
}

void RpWindow::setGeometry(QRect rect) {
	_helper->setGeometry(rect);
}

void RpWindow::showFullScreen() {
	_helper->showFullScreen();
}

void RpWindow::showNormal() {
	_helper->showNormal();
}

void RpWindow::close() {
	_helper->close();
}

void RpWindow::setBodyTitleArea(
		Fn<WindowTitleHitTestFlags(QPoint)> testMethod) {
	_helper->setBodyTitleArea(std::move(testMethod));
}

} // namespace Ui
