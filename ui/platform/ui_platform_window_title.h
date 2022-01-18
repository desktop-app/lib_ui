// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

#include <QtCore/QRect>
#include <QtCore/QPoint>

namespace style {
struct WindowTitle;
} // namespace style

namespace Ui {

class IconButton;
class PlainShadow;
class RpWindow;

namespace Platform {

class TitleControls;

enum class HitTestResult {
	None = 0,
	Client,
	Minimize,
	MaximizeRestore,
	Close,
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

struct HitTestRequest {
	QPoint point;
	HitTestResult result = HitTestResult::Client;
};

[[nodiscard]] bool SemiNativeSystemButtonProcessing();
void SetupSemiNativeSystemButtons(
	not_null<TitleControls*> controls,
	not_null<RpWindow*> window,
	rpl::lifetime &lifetime,
	Fn<bool()> filter = nullptr);

class TitleControls final {
public:
	TitleControls(
		not_null<RpWidget*> parent,
		const style::WindowTitle &st,
		Fn<void(bool maximized)> maximize = nullptr);

	void setStyle(const style::WindowTitle &st);
	[[nodiscard]] not_null<const style::WindowTitle*> st() const;
	[[nodiscard]] QRect geometry() const;
	void setResizeEnabled(bool enabled);
	void raise();

	[[nodiscard]] HitTestResult hitTest(QPoint point) const;

	void buttonOver(HitTestResult testResult);
	void buttonDown(HitTestResult testResult);

	enum class Control {
		Unknown,
		Minimize,
		Maximize,
		Close,
	};

	struct Layout {
		std::vector<Control> left;
		std::vector<Control> right;
	};

private:
	class Button;

	[[nodiscard]] not_null<RpWidget*> parent() const;
	[[nodiscard]] not_null<QWidget*> window() const;
	[[nodiscard]] Button *controlWidget(Control control) const;

	void init(Fn<void(bool maximized)> maximize);
	void subscribeToStateChanges();
	void updateButtonsState();
	void updateControlsPosition();
	void updateControlsPositionBySide(
		const std::vector<Control> &controls,
		bool right);
	void handleWindowStateChanged(Qt::WindowState state = Qt::WindowNoState);

	not_null<const style::WindowTitle*> _st;

	object_ptr<Button> _minimize;
	object_ptr<Button> _maximizeRestore;
	object_ptr<Button> _close;

	bool _maximizedState = false;
	bool _activeState = false;
	bool _resizeEnabled = true;

};

class DefaultTitleWidget : public RpWidget {
public:
	explicit DefaultTitleWidget(not_null<RpWidget*> parent);

	[[nodiscard]] not_null<const style::WindowTitle*> st() const;
	void setText(const QString &text);
	void setStyle(const style::WindowTitle &st);
	void setResizeEnabled(bool enabled);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	TitleControls _controls;
	object_ptr<Ui::PlainShadow> _shadow;
	bool _mousePressed = false;

};

struct SeparateTitleControls {
	SeparateTitleControls(
		QWidget *parent,
		const style::WindowTitle &st,
		Fn<void(bool maximized)> maximize);

	RpWidget wrap;
	TitleControls controls;
};

[[nodiscard]] auto SetupSeparateTitleControls(
	not_null<RpWindow*> window,
	const style::WindowTitle &st,
	Fn<void(bool maximized)> maximize = nullptr)
-> std::unique_ptr<SeparateTitleControls>;

} // namespace Platform
} // namespace Ui
