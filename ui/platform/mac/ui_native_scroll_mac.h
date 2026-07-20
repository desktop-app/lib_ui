// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QWheelEvent>

#include <optional>

namespace Ui::Platform {

// Qt rounds NSEvent's fractional scrollingDelta to whole logical pixels
// when filling QWheelEvent::pixelDelta(). Returns the exact fractional
// delta (in logical points, unscaled) recorded from the native event
// stream for this wheel event, matched by timestamp; nullopt when no
// confident match exists.
[[nodiscard]] std::optional<QPointF> LookupNativeScrollDelta(
	not_null<const QWheelEvent*> e);

} // namespace Ui::Platform
