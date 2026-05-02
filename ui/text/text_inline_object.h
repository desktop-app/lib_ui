// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <gsl/span>

#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QImage>

namespace Ui::Text {

enum class InlineObjectVerticalAlign : uchar {
	AscentDescent = 0,
	CenterInText = 1,
};

struct InlineObjectDescriptor {
	int width = 0;
	int ascent = 0;
	int descent = 0;
	InlineObjectVerticalAlign align = InlineObjectVerticalAlign::AscentDescent;
	QString copySource;
	QString fallbackText;
	QImage image;
	bool colorizeToTextColor = false;
	mutable QImage colorizedImage;
	mutable QColor colorizedImageColor;
	mutable QSize colorizedImageSize;

	[[nodiscard]] QStringView textForCopy() const {
		return !copySource.isEmpty()
			? QStringView(copySource)
			: QStringView(fallbackText);
	}
};

struct InlineObjectPlacement {
	int position = 0;
	InlineObjectDescriptor object;
};

using InlineObjectPlacements = gsl::span<const InlineObjectPlacement>;

} // namespace Ui::Text
