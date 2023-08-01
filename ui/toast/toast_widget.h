// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/toast/toast.h"
#include "ui/text/text.h"
#include "ui/rp_widget.h"
#include "ui/round_rect.h"

namespace Ui {
namespace Toast {
namespace internal {

class Widget final : public RpWidget {
public:
	Widget(QWidget *parent, const Config &config);

	// shownLevel=1 completely visible, shownLevel=0 completely invisible
	void setShownLevel(float64 shownLevel);
	void setInputUsed(bool used);

	void onParentResized();

protected:
	void paintEvent(QPaintEvent *e) override;

	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	[[nodiscard]] int widthWithoutPadding(int w) const;
	void updateGeometry();

	const not_null<const style::Toast*> _st;
	RoundRect _roundRect;
	RectPart _slideSide = RectPart::None;

	float64 _shownLevel = 0;
	bool _multiline = false;
	bool _dark = false;
	bool _processMouse = false;
	bool _adaptive = false;

	int _maxTextWidth = 0;
	int _maxTextHeight = 0;
	int _textWidth = 0;
	int _textHeight = 0;
	int _textTop = 0;
	Text::String _text;

	ClickHandlerFilter _clickHandlerFilter;

};

} // namespace internal
} // namespace Toast
} // namespace Ui
