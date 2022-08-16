// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

class SpoilerClickHandler;

namespace Ui::Text {

struct SpoilerData {
	struct {
		std::array<QImage, 4> corners;
		QColor color;
	} spoilerCache, spoilerShownCache;

	QVector<std::shared_ptr<SpoilerClickHandler>> links;
};

} // namespace Ui::Text
