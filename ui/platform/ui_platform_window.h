// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flags.h"

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {

class RpWidget;
enum class WindowTitleHitTestFlag;
using WindowTitleHitTestFlags = base::flags<WindowTitleHitTestFlag>;

namespace Platform {

class BasicWindowHelper {
public:
	explicit BasicWindowHelper(not_null<RpWidget*> window);
	virtual ~BasicWindowHelper() = default;

	[[nodiscard]] virtual not_null<RpWidget*> body();
	virtual void setTitle(const QString &title);
	virtual void setTitleStyle(const style::WindowTitle &st);
	virtual void setMinimumSize(QSize size);
	virtual void setFixedSize(QSize size);
	virtual void setGeometry(QRect rect);
	virtual void showFullScreen();
	virtual void showNormal();
	virtual void close();

	void setBodyTitleArea(Fn<WindowTitleHitTestFlags(QPoint)> testMethod);

protected:
	[[nodiscard]] not_null<RpWidget*> window() const {
		return _window;
	}
	[[nodiscard]] WindowTitleHitTestFlags bodyTitleAreaHit(
			QPoint point) const {
		return _bodyTitleAreaTestMethod
			? _bodyTitleAreaTestMethod(point)
			: WindowTitleHitTestFlag();
	}

private:
	virtual void setupBodyTitleAreaEvents();

	const not_null<RpWidget*> _window;
	Fn<WindowTitleHitTestFlags(QPoint)> _bodyTitleAreaTestMethod;
	bool _mousePressed = false;

};

[[nodiscard]] std::unique_ptr<BasicWindowHelper> CreateSpecialWindowHelper(
	not_null<RpWidget*> window);

[[nodiscard]] inline std::unique_ptr<BasicWindowHelper> CreateWindowHelper(
		not_null<RpWidget*> window) {
	if (auto special = CreateSpecialWindowHelper(window)) {
		return special;
	}
	return std::make_unique<BasicWindowHelper>(window);
}

} // namespace Platform
} // namespace Ui
