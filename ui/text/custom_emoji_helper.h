// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"

namespace Ui::Text {

struct ImageEmoji {
	QImage image;
	QMargins margin;
	bool textColor = true;
};

struct PaletteDependentEmoji {
	Fn<QImage()> factory;
	QMargins margin;
};

class CustomEmojiHelper final {
public:
	CustomEmojiHelper();
	CustomEmojiHelper(MarkedContext parent);

	[[nodiscard]] QString imageData(ImageEmoji emoji);
	[[nodiscard]] TextWithEntities image(ImageEmoji emoji);

	[[nodiscard]] QString paletteDependentData(PaletteDependentEmoji emoji);
	[[nodiscard]] TextWithEntities paletteDependent(
		PaletteDependentEmoji emoji);

	[[nodiscard]] MarkedContext context(Fn<void()> repaint = nullptr);

private:
	struct Data {
		QString prefix;
		std::vector<QImage> images;
		std::vector<Fn<QImage()>> paletteDependent;
	};

	[[nodiscard]] not_null<Data*> ensureData();

	MarkedContext _parent;
	std::shared_ptr<Data> _data;

};

} // namespace Ui::Text
