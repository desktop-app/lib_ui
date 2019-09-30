// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
namespace Platform {
class BasicWindowHelper;
} // namespace Platform

class Window : public RpWidget {
public:
	explicit Window(QWidget *parent = nullptr);
	~Window();

	[[nodiscard]] not_null<RpWidget*> body();
	[[nodiscard]] not_null<const RpWidget*> body() const;

	void setTitle(const QString &title);
	void setSizeMin(QSize size);
	void setGeometry(QRect rect);

private:
	const std::unique_ptr<Platform::BasicWindowHelper> _helper;

};

} // namespace Ui
