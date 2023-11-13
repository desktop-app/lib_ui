// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_window.h"
#include "ui/platform/win/ui_window_shadow_win.h"

namespace Ui {
namespace Platform {

class TitleWidget;
struct HitTestRequest;
enum class HitTestResult;
class DirectManipulation;
struct DirectManipulationEvent;

class WindowHelper final : public BasicWindowHelper {
public:
	explicit WindowHelper(not_null<RpWidget*> window);
	~WindowHelper();

	void initInWindow(not_null<RpWindow*> window) override;
	not_null<RpWidget*> body() override;
	QMargins frameMargins() override;
	int additionalContentPadding() const override;
	rpl::producer<int> additionalContentPaddingValue() const override;
	void setTitle(const QString &title) override;
	void setTitleStyle(const style::WindowTitle &st) override;
	void setNativeFrame(bool enabled) override;
	void setMinimumSize(QSize size) override;
	void setFixedSize(QSize size) override;
	void setGeometry(QRect rect) override;
	void showFullScreen() override;
	void showNormal() override;

	[[nodiscard]] auto hitTestRequests() const
		-> rpl::producer<not_null<HitTestRequest*>> override;
	[[nodiscard]] auto systemButtonOver() const
		-> rpl::producer<HitTestResult> override;
	[[nodiscard]] auto systemButtonDown() const
		-> rpl::producer<HitTestResult> override;

private:
	class NativeFilter;
	friend class NativeFilter;

	void init();
	void updateMargins();
	void updateWindowFrameColors();
	void updateWindowFrameColors(bool active);
	void initialShadowUpdate();
	void updateCornersRounding();
	void fixMaximizedWindow();
	void handleDirectManipulationEvent(
		const DirectManipulationEvent &event);
	[[nodiscard]] bool handleNativeEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);
	[[nodiscard]] bool handleSystemButtonEvent(
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);
	[[nodiscard]] bool fixedSize() const;
	[[nodiscard]] int systemButtonHitTest(HitTestResult result) const;
	[[nodiscard]] HitTestResult systemButtonHitTest(int result) const;

	[[nodiscard]] int titleHeight() const;
	static not_null<NativeFilter*> GetNativeFilter();

	const HWND _handle = nullptr;
	const not_null<TitleWidget*> _title;
	const not_null<RpWidget*> _body;
	std::unique_ptr<DirectManipulation> _directManipulation;
	rpl::event_stream<not_null<HitTestRequest*>> _hitTestRequests;
	rpl::event_stream<HitTestResult> _systemButtonOver;
	rpl::event_stream<HitTestResult> _systemButtonDown;
	std::optional<WindowShadow> _shadow;
	rpl::variable<uint> _dpi;
	QMargins _marginsDelta;
	bool _updatingMargins = false;
	bool _isFullScreen = false;
	bool _isMaximizedAndTranslucent = false;

};

[[nodiscard]] HWND GetWindowHandle(not_null<QWidget*> widget);
[[nodiscard]] HWND GetWindowHandle(not_null<QWindow*> window);

void SendWMPaintForce(not_null<QWidget*> widget);
void SendWMPaintForce(not_null<QWindow*> window);

} // namespace Platform
} // namespace Ui
