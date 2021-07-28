// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "base/flags.h"

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {
namespace Platform {
class BasicWindowHelper;
} // namespace Platform

enum class WindowTitleHitTestFlag {
	None       = 0x00,
	Move       = 0x01,
	Maximize   = 0x02,
	FullScreen = 0x04,
};
inline constexpr bool is_flag_type(WindowTitleHitTestFlag) {
	return true;
}
using WindowTitleHitTestFlags = base::flags<WindowTitleHitTestFlag>;

class Window : public RpWidget {
public:
	explicit Window(QWidget *parent = nullptr);
	~Window();

	[[nodiscard]] not_null<RpWidget*> body();
	[[nodiscard]] not_null<const RpWidget*> body() const;

	void setTitle(const QString &title);
	void setTitleStyle(const style::WindowTitle &st);
	void setNativeFrame(bool enabled);
	void setMinimumSize(QSize size);
	void setFixedSize(QSize size);
	void setStaysOnTop(bool enabled);
	void setGeometry(QRect rect);
	void showFullScreen();
	void showNormal();
	void close();
	void setBodyTitleArea(Fn<WindowTitleHitTestFlags(QPoint)> testMethod);

private:
	const std::unique_ptr<Platform::BasicWindowHelper> _helper;

};

} // namespace Ui
