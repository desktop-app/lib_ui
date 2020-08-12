// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {

class RpWidget;

namespace Platform {

class BasicWindowHelper {
public:
	explicit BasicWindowHelper(not_null<RpWidget*> window);

	[[nodiscard]] virtual not_null<RpWidget*> body();
	virtual void setTitle(const QString &title);
	virtual void setTitleStyle(const style::WindowTitle &st);
	virtual void setMinimumSize(QSize size);
	virtual void setFixedSize(QSize size);
	virtual void setGeometry(QRect rect);
	virtual ~BasicWindowHelper() = default;

	void setBodyTitleArea(Fn<bool(QPoint)> testMethod);

protected:
	[[nodiscard]] not_null<RpWidget*> window() const {
		return _window;
	}
	[[nodiscard]] bool bodyTitleAreaHit(QPoint point) const {
		return _bodyTitleAreaTestMethod && _bodyTitleAreaTestMethod(point);
	}
	[[nodiscard]] virtual bool customBodyTitleAreaHandling() {
		return false;
	}

private:
	const not_null<RpWidget*> _window;
	Fn<bool(QPoint)> _bodyTitleAreaTestMethod;

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
