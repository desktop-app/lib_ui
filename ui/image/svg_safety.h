// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

class QByteArray;

namespace Images {

// Checks an SVG document against everything we know can crash or hang
// QSvgRenderer (any Qt version) and returns the bytes unchanged if they
// are safe to parse and render, or an empty array if they are not.
[[nodiscard]] QByteArray SanitizeSvg(const QByteArray &bytes);

} // namespace Images
