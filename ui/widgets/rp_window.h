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
struct TextStyle;
} // namespace style

namespace Ui {
namespace Platform {
class BasicWindowHelper;
struct HitTestRequest;
enum class HitTestResult;
} // namespace Platform

enum class WindowTitleHitTestFlag {
	None       = 0x00,
	Move       = 0x01,
	Menu       = 0x02,
	Maximize   = 0x04,
	FullScreen = 0x08,
};
inline constexpr bool is_flag_type(WindowTitleHitTestFlag) {
	return true;
}
using WindowTitleHitTestFlags = base::flags<WindowTitleHitTestFlag>;

class RpWindow : public RpWidget {
public:
	explicit RpWindow(QWidget *parent = nullptr);
	~RpWindow();

	[[nodiscard]] not_null<RpWidget*> body();
	[[nodiscard]] not_null<const RpWidget*> body() const;
	[[nodiscard]] QMargins frameMargins() const;

	// In Windows 11 the window rounding shadow takes about
	// round(1px * system_scale) from the window geometry on each side.
	//
	// Top shift is made by the TitleWidget height, but the rest of the
	// side shifts are left for the RpWindow client to consider.
	[[nodiscard]] int additionalContentPadding() const;
	[[nodiscard]] rpl::producer<int> additionalContentPaddingValue() const;

	[[nodiscard]] auto hitTestRequests() const
		-> rpl::producer<not_null<Platform::HitTestRequest*>>;
	[[nodiscard]] auto systemButtonOver() const
		-> rpl::producer<Platform::HitTestResult>;
	[[nodiscard]] auto systemButtonDown() const
		-> rpl::producer<Platform::HitTestResult>;
	void overrideSystemButtonOver(Platform::HitTestResult button);
	void overrideSystemButtonDown(Platform::HitTestResult button);

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
	[[nodiscard]] int manualRoundingRadius() const;
	void setBodyTitleArea(Fn<WindowTitleHitTestFlags(QPoint)> testMethod);

	[[nodiscard]] const style::TextStyle &titleTextStyle() const;

private:
	const std::unique_ptr<Platform::BasicWindowHelper> _helper;

};

} // namespace Ui
