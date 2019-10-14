// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Ui {

class RpWidget;

namespace Platform {

class BasicWindowHelper {
public:
	[[nodiscard]] virtual not_null<RpWidget*> body() = 0;
	virtual void setTitle(const QString &title) = 0;
	virtual void setMinimumSize(QSize size) = 0;
	virtual void setFixedSize(QSize size) = 0;
	virtual void setGeometry(QRect rect) = 0;
	virtual ~BasicWindowHelper() = default;

};

[[nodiscard]] std::unique_ptr<BasicWindowHelper> CreateWindowHelper(
	not_null<RpWidget*> window);

} // namespace Platform
} // namespace Ui
