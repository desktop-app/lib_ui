// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "styles/style_widgets.h"

class Painter;

namespace Ui {

class CrossAnimation {
public:
	static void paint(
		QPainter &p,
		const style::CrossAnimation &st,
		style::color color,
		int x,
		int y,
		int outerWidth,
		float64 shown,
		float64 loading = 0.);
	static void paintStaticLoading(
		QPainter &p,
		const style::CrossAnimation &st,
		style::color color,
		int x,
		int y,
		int outerWidth,
		float64 shown);

};

} // namespace Ui
