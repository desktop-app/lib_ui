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

class PlainShadow;

namespace Platform {

class TitleWidget : public RpWidget {
public:
	TitleWidget(not_null<RpWidget*> parent, int height);

	void setText(const QString &text);
	void setStyle(const style::WindowTitle &st);
	[[nodiscard]] QString text() const;
	[[nodiscard]] bool shouldBeHidden() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	not_null<RpWidget*> window() const;

	void init(int height);

	not_null<const style::WindowTitle*> _st;
	object_ptr<Ui::PlainShadow> _shadow;
	QString _text;
	QFont _font;

};

} // namespace Platform
} // namespace Ui
