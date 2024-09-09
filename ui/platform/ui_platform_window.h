// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flags.h"
#include "base/object_ptr.h"
#include "ui/round_rect.h"

namespace style {
struct WindowTitle;
struct TextStyle;
} // namespace style

namespace Ui {

class RpWidget;
class RpWindow;
enum class WindowTitleHitTestFlag;
using WindowTitleHitTestFlags = base::flags<WindowTitleHitTestFlag>;

namespace Platform {

struct HitTestRequest;
enum class HitTestResult;
class DefaultTitleWidget;

class BasicWindowHelper {
public:
	explicit BasicWindowHelper(not_null<RpWidget*> window);
	virtual ~BasicWindowHelper() = default;

	[[nodiscard]] not_null<RpWidget*> window() const {
		return _window;
	}

	virtual void initInWindow(not_null<RpWindow*> window);
	[[nodiscard]] virtual not_null<RpWidget*> body();
	[[nodiscard]] virtual QMargins frameMargins();
	[[nodiscard]] virtual int additionalContentPadding() const;
	[[nodiscard]] virtual auto additionalContentPaddingValue() const
		-> rpl::producer<int>;
	[[nodiscard]] virtual auto hitTestRequests() const
		-> rpl::producer<not_null<HitTestRequest*>>;
	[[nodiscard]] virtual auto systemButtonOver() const
		-> rpl::producer<HitTestResult>;
	[[nodiscard]] virtual auto systemButtonDown() const
		-> rpl::producer<HitTestResult>;
	virtual void overrideSystemButtonOver(HitTestResult button);
	virtual void overrideSystemButtonDown(HitTestResult button);
	virtual void setTitle(const QString &title);
	virtual void setTitleStyle(const style::WindowTitle &st);
	virtual void setNativeFrame(bool enabled);
	virtual void setMinimumSize(QSize size);
	virtual void setFixedSize(QSize size);
	virtual void setStaysOnTop(bool enabled);
	virtual void setGeometry(QRect rect);
	virtual void showFullScreen();
	virtual void showNormal();
	virtual void close();

	virtual int manualRoundingRadius() const;
	void setBodyTitleArea(Fn<WindowTitleHitTestFlags(QPoint)> testMethod);
	[[nodiscard]] bool mousePressCancelled() const {
		return _mousePressCancelled;
	}

	[[nodiscard]] virtual const style::TextStyle &titleTextStyle() const;

protected:
	[[nodiscard]] WindowTitleHitTestFlags bodyTitleAreaHit(
			QPoint point) const {
		return _bodyTitleAreaTestMethod
			? _bodyTitleAreaTestMethod(point)
			: WindowTitleHitTestFlag();
	}
	[[nodiscard]] QMargins nativeFrameMargins() const;

private:
	virtual void setupBodyTitleAreaEvents();

	const not_null<RpWidget*> _window;
	Fn<WindowTitleHitTestFlags(QPoint)> _bodyTitleAreaTestMethod;
	bool _mousePressed = false;
	bool _mousePressCancelled = false;

};

class DefaultWindowHelper final : public QObject, public BasicWindowHelper {
public:
	explicit DefaultWindowHelper(not_null<RpWidget*> window);

	not_null<RpWidget*> body() override;
	QMargins frameMargins() override;
	void setTitle(const QString &title) override;
	void setTitleStyle(const style::WindowTitle &st) override;
	void setNativeFrame(bool enabled) override;
	void setMinimumSize(QSize size) override;
	void setFixedSize(QSize size) override;
	void setGeometry(QRect rect) override;
	int manualRoundingRadius() const override;

protected:
	bool eventFilter(QObject *obj, QEvent *e) override;

private:
	void init();
	void updateRoundingOverlay();
	[[nodiscard]] bool hasShadow() const;
	[[nodiscard]] QMargins resizeArea() const;
	[[nodiscard]] Qt::Edges edgesFromPos(const QPoint &pos) const;
	void paintBorders(QPainter &p);
	void updateWindowMargins();
	void updateCursor(Qt::Edges edges);
	[[nodiscard]] int titleHeight() const;
	[[nodiscard]] QMargins bodyPadding() const;

	const not_null<DefaultTitleWidget*> _title;
	const not_null<RpWidget*> _body;
	RoundRect _roundRect;
	std::array<QImage, 4> _sides;
	std::array<QImage, 4> _corners;
	object_ptr<RpWidget> _roundingOverlay = { nullptr };
	rpl::variable<Qt::WindowStates> _windowState = Qt::WindowNoState;
	QRect _lastGeometry;
	std::optional<QSize> _minimumSize;
	std::optional<QSize> _fixedSize;
	bool _marginsSet = false;

};

[[nodiscard]] std::unique_ptr<BasicWindowHelper> CreateSpecialWindowHelper(
	not_null<RpWidget*> window);

[[nodiscard]] inline std::unique_ptr<BasicWindowHelper> CreateWindowHelper(
		not_null<RpWidget*> window) {
	if (auto special = CreateSpecialWindowHelper(window)) {
		return special;
	}
	return std::make_unique<DefaultWindowHelper>(window);
}

[[nodiscard]] bool NativeWindowFrameSupported();

} // namespace Platform
} // namespace Ui
