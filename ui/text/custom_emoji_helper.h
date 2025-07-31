// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"

namespace Ui::Text {

class CustomEmojiHelper final {
public:
	CustomEmojiHelper();
	CustomEmojiHelper(MarkedContext parent);

	[[nodiscard]] QString imageData(QImage image, QMargins padding = {});
	[[nodiscard]] TextWithEntities image(
		QImage image,
		QMargins padding = {});

	[[nodiscard]] QString paletteDependentData(
		Fn<QImage()> factory,
		QMargins padding = {});
	[[nodiscard]] TextWithEntities paletteDependent(
		Fn<QImage()> factory,
		QMargins padding = {});

	[[nodiscard]] MarkedContext context();

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
