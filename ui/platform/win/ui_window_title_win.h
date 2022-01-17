// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/platform/ui_platform_window_title.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"

#include <QtCore/QRect>
#include <QtCore/QPoint>

#include <Windows.h> // HWND, WINDOWPLACEMENT

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {

class IconButton;
class PlainShadow;

namespace Platform {

enum class HitTestResult {
	None = 0,
	Client,
	SysButton,
	Caption,
	Top,
	TopRight,
	Right,
	BottomRight,
	Bottom,
	BottomLeft,
	Left,
	TopLeft,
};

class TitleWidget : public RpWidget {
public:
	explicit TitleWidget(not_null<RpWidget*> parent);
	~TitleWidget();

	void setText(const QString &text);
	void setStyle(const style::WindowTitle &st);
	[[nodiscard]] not_null<const style::WindowTitle*> st() const;
	[[nodiscard]] HitTestResult hitTest(QPoint point) const;
	void setResizeEnabled(bool enabled);

	void refreshAdditionalPaddings();
	void refreshAdditionalPaddings(HWND handle);
	void refreshAdditionalPaddings(
		HWND handle,
		const WINDOWPLACEMENT &placement);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setVisibleHook(bool visible) override;

private:
	struct PaddingHelper;

	[[nodiscard]] bool additionalPaddingRequired() const;
	void refreshGeometryWithWidth(int width);
	void setAdditionalPadding(int padding);

	std::unique_ptr<PaddingHelper> _paddingHelper;
	TitleControls _controls;
	object_ptr<Ui::PlainShadow> _shadow;

};

[[nodiscard]] bool CheckTitlePaddingRequired();

} // namespace Platform
} // namespace Ui
