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
class AbstractButton;
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

enum class TitleControl {
	Unknown,
	Minimize,
	Maximize,
	Close,
};

class AbstractTitleButtons {
public:
	[[nodiscard]] virtual object_ptr<AbstractButton> create(
		not_null<QWidget*> parent,
		TitleControl control,
		const style::WindowTitle &st) = 0;
	virtual void updateState(
		bool active,
		bool maximized,
		const style::WindowTitle &st) = 0;
	virtual void notifySynteticOver(TitleControl control, bool over) = 0;

	virtual ~AbstractTitleButtons() = default;
};

class IconTitleButtons final : public AbstractTitleButtons {
public:
	object_ptr<AbstractButton> create(
		not_null<QWidget*> parent,
		TitleControl control,
		const style::WindowTitle &st) override;
	void updateState(
		bool active,
		bool maximized,
		const style::WindowTitle &st) override;
	void notifySynteticOver(TitleControl control, bool over) override {
	}

private:
	QPointer<IconButton> _minimize;
	QPointer<IconButton> _maximizeRestore;
	QPointer<IconButton> _close;

};

class TitleControls final {
public:
	TitleControls(
		not_null<RpWidget*> parent,
		const style::WindowTitle &st,
		Fn<void(bool maximized)> maximize = nullptr);
	TitleControls(
		not_null<RpWidget*> parent,
		const style::WindowTitle &st,
		std::unique_ptr<AbstractTitleButtons> buttons,
		Fn<void(bool maximized)> maximize = nullptr);

	void setStyle(const style::WindowTitle &st);
	[[nodiscard]] not_null<const style::WindowTitle*> st() const;
	[[nodiscard]] QRect geometry() const;
	void setResizeEnabled(bool enabled);
	void raise();

	[[nodiscard]] HitTestResult hitTest(QPoint point, int padding) const;

	void buttonOver(HitTestResult testResult);
	void buttonDown(HitTestResult testResult);

	using Control = TitleControl;
	struct Layout {
		std::vector<Control> left;
		std::vector<Control> right;
	};

private:
	[[nodiscard]] not_null<RpWidget*> parent() const;
	[[nodiscard]] not_null<QWidget*> window() const;
	[[nodiscard]] AbstractButton *controlWidget(Control control) const;

	void init(Fn<void(bool maximized)> maximize);
	void updateButtonsState();
	void updateControlsPosition();
	void updateControlsPositionBySide(
		const std::vector<Control> &controls,
		bool right);
	void handleWindowStateChanged(Qt::WindowStates state = Qt::WindowNoState);

	not_null<const style::WindowTitle*> _st;
	const std::unique_ptr<AbstractTitleButtons> _buttons;

	object_ptr<AbstractButton> _minimize;
	object_ptr<AbstractButton> _maximizeRestore;
	object_ptr<AbstractButton> _close;

	bool _maximizedState = false;
	bool _activeState = false;
	bool _resizeEnabled = true;

};

namespace internal {

// Actual requestor, cached by the public interface
[[nodiscard]] TitleControls::Layout TitleControlsLayout();
void NotifyTitleControlsLayoutChanged(
	const std::optional<TitleControls::Layout> &layout = std::nullopt);

} // namespace internal

[[nodiscard]] TitleControls::Layout TitleControlsLayout();
[[nodiscard]] rpl::producer<TitleControls::Layout> TitleControlsLayoutValue();
[[nodiscard]] rpl::producer<TitleControls::Layout> TitleControlsLayoutChanged();
[[nodiscard]] inline bool TitleControlsOnLeft(
		const TitleControls::Layout &layout = TitleControlsLayout()) {
	if (ranges::contains(layout.left, TitleControl::Close)) {
		return true;
	} else if (ranges::contains(layout.right, TitleControl::Close)) {
		return false;
	} else if (layout.left.size() > layout.right.size()) {
		return true;
	}
	return false;
}

class DefaultTitleWidget : public RpWidget {
public:
	explicit DefaultTitleWidget(not_null<RpWidget*> parent);

	[[nodiscard]] not_null<const style::WindowTitle*> st() const;
	[[nodiscard]] QRect controlsGeometry() const;
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
	SeparateTitleControls(
		QWidget *parent,
		const style::WindowTitle &st,
		std::unique_ptr<AbstractTitleButtons> buttons,
		Fn<void(bool maximized)> maximize);

	RpWidget wrap;
	TitleControls controls;
};

[[nodiscard]] auto SetupSeparateTitleControls(
	not_null<RpWindow*> window,
	const style::WindowTitle &st,
	Fn<void(bool maximized)> maximize = nullptr,
	rpl::producer<int> controlsTop = nullptr)
-> std::unique_ptr<SeparateTitleControls>;

[[nodiscard]] auto SetupSeparateTitleControls(
	not_null<RpWindow*> window,
	std::unique_ptr<SeparateTitleControls> created,
	rpl::producer<int> controlsTop = nullptr)
-> std::unique_ptr<SeparateTitleControls>;

} // namespace Platform
} // namespace Ui
