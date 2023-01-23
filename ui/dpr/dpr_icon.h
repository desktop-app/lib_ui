// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core.h"

class QImage;

namespace dpr {

[[nodiscard]] QImage IconFrame(
	const style::icon &icon,
	const QColor &color,
	double ratio);

} // namespace dpr
