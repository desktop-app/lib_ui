// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "styles/style_widgets.h"

#include <deque>

namespace Ui {

class RippleAnimation {
public:
	// White upon transparent mask, like colorizeImage(black-white-mask, white).
	RippleAnimation(const style::RippleAnimation &st, QImage mask, Fn<void()> update);

	void add(QPoint origin, int startRadius = 0);
	void addFading();
	void lastStop();
	void lastUnstop();
	void lastFinish();
	void forceRepaint();

	void paint(QPainter &p, int x, int y, int outerWidth, const QColor *colorOverride = nullptr);

	bool empty() const {
		return _ripples.empty();
	}

	static QImage maskByDrawer(QSize size, bool filled, Fn<void(QPainter &p)> drawer);
	static QImage rectMask(QSize size);
	static QImage roundRectMask(QSize size, int radius);
	static QImage ellipseMask(QSize size);

	~RippleAnimation();

private:
	void clear();
	void clearFinished();

	const style::RippleAnimation &_st;
	QPixmap _mask;
	Fn<void()> _update;

	class Ripple;
	std::deque<std::unique_ptr<Ripple>> _ripples;

};

} // namespace Ui
