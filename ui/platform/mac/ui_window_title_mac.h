// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"

#include <QtCore/QRect>
#include <QtCore/QPoint>

namespace style {
struct WindowTitle;
struct TextStyle;
} // namespace style

namespace Ui {

class PlainShadow;

namespace Platform {

class TitleWidget : public RpWidget {
public:
	TitleWidget(not_null<RpWidget*> parent, int height);
	~TitleWidget();

	void setText(const QString &text);
	void setStyle(const style::WindowTitle &st);
	void setControlsRect(const QRect &rect);
	[[nodiscard]] QString text() const;
	[[nodiscard]] bool shouldBeHidden() const;
	[[nodiscard]] const style::TextStyle &textStyle() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	not_null<RpWidget*> window() const;

	void init(int height);

	not_null<const style::WindowTitle*> _st;
	std::unique_ptr<style::TextStyle> _textStyle;
	object_ptr<Ui::PlainShadow> _shadow;
	QString _text;
	Ui::Text::String _string;
	int _controlsRight = 0;

};

} // namespace Platform
} // namespace Ui
