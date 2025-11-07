// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/custom_emoji_helper.h"

#include "ui/text/custom_emoji_instance.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"

namespace Ui::Text {
namespace {

[[nodiscard]] QString Prefix(int counter) {
	return u"helper%1:"_q.arg(counter);
}

[[nodiscard]] QString PaddingPostfix(QMargins padding) {
	if (padding.isNull()) {
		return QString();
	}
	return u":%1,%2,%3,%4"_q
		.arg(padding.left())
		.arg(padding.top())
		.arg(padding.right())
		.arg(padding.bottom());
}

[[nodiscard]] QMargins PaddingFromPostfix(QStringView postfix) {
	const auto parts = postfix.split(u',');
	if (parts.size() != 4) {
		return {};
	}
	return QMargins(
		parts[0].toInt(),
		parts[1].toInt(),
		parts[2].toInt(),
		parts[3].toInt());
}

} // namespace

CustomEmojiHelper::CustomEmojiHelper() = default;

CustomEmojiHelper::CustomEmojiHelper(MarkedContext parent)
: _parent(std::move(parent)) {
}

QString CustomEmojiHelper::imageData(ImageEmoji emoji) {
	Expects(!emoji.image.isNull());

	const auto data = ensureData();
	const auto result = data->prefix
		+ u"image%1"_q.arg(data->images.size())
		+ (emoji.margin.isNull() ? QString() : PaddingPostfix(emoji.margin))
		+ (emoji.textColor
			? (emoji.margin.isNull() ? u"::1"_q : u":1"_q)
			: QString());
	data->images.push_back(std::move(emoji.image));
	return result;
}

TextWithEntities CustomEmojiHelper::image(ImageEmoji emoji){
	return SingleCustomEmoji(imageData(std::move(emoji)));
}

QString CustomEmojiHelper::paletteDependentData(
		PaletteDependentEmoji emoji) {
	Expects(emoji.factory != nullptr);

	const auto data = ensureData();
	const auto result = data->prefix
		+ u"factory%1"_q.arg(data->paletteDependent.size())
		+ (emoji.margin.isNull() ? QString() : PaddingPostfix(emoji.margin));
	data->paletteDependent.push_back(std::move(emoji.factory));
	return result;
}

TextWithEntities CustomEmojiHelper::paletteDependent(
		PaletteDependentEmoji emoji) {
	return SingleCustomEmoji(paletteDependentData(std::move(emoji)));
}

MarkedContext CustomEmojiHelper::context(Fn<void()> repaint) {
	auto result = _parent;
	if (repaint) {
		result.repaint = std::move(repaint);
	}
	if (!_data) {
		return result;
	}
	auto factory = [map = _data](
		QStringView data,
		const MarkedContext &context
	) -> std::unique_ptr<CustomEmoji> {
		if (!data.startsWith(map->prefix)) {
			return nullptr;
		}
		const auto id = data.mid(map->prefix.size());
		const auto parts = id.split(':');
		if (parts.empty()) {
			return nullptr;
		}
		const auto type = parts[0];
		const auto postfix = (parts.size() > 1) ? parts[1] : QStringView();
		const auto padding = PaddingFromPostfix(postfix);
		if (type.startsWith(u"image"_q)) {
			const auto index = type.mid(u"image"_q.size()).toInt();
			if (index >= 0 && index < map->images.size()) {
				return std::make_unique<Ui::CustomEmoji::Internal>(
					data.toString(),
					base::duplicate(map->images[index]),
					padding,
					(parts.size() > 2) && parts[2] == u"1"_q);
			}
		} else if (type.startsWith(u"factory"_q)) {
			const auto index = type.mid(u"factory"_q.size()).toInt();
			if (index >= 0 && index < map->paletteDependent.size()) {
				return std::make_unique<PaletteDependentCustomEmoji>(
					map->paletteDependent[index],
					data.toString(),
					padding);
			}
		}
		return nullptr;
	};
	if (auto old = _parent.customEmojiFactory) {
		result.customEmojiFactory = [
			factory = std::move(factory),
			old = std::move(old)
		](
			QStringView data,
			const MarkedContext &context
		) -> std::unique_ptr<CustomEmoji> {
			if (auto result = factory(data, context)) {
				return result;
			}
			return old(data, context);
		};
	} else {
		result.customEmojiFactory = factory;
	}
	return result;
}

auto CustomEmojiHelper::ensureData() -> not_null<Data*> {
	if (!_data) {
		static auto counter = std::atomic<int>();
		_data = std::make_shared<Data>();
		_data->prefix = Prefix(counter++);
	}
	return _data.get();
}

} // namespace Ui::Text
