// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flags.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/style/style_core.h"
#include "ui/emoji_config.h"

#include <crl/crl_time.h>

#include <private/qfixed_p.h>

namespace style {
struct TextStyle;
} // namespace style

namespace Ui::Text {

enum class TextBlockType : uint16 {
	Newline     = 0x01,
	Text        = 0x02,
	Emoji       = 0x03,
	CustomEmoji = 0x04,
	Skip        = 0x05,
};

enum class TextBlockFlag : uint16 {
	Bold       = 0x001,
	Italic     = 0x002,
	Underline  = 0x004,
	StrikeOut  = 0x008,
	Tilde      = 0x010, // Tilde fix in OpenSans.
	Semibold   = 0x020,
	Code       = 0x040,
	Pre        = 0x080,
	Spoiler    = 0x100,
	Blockquote = 0x200,
};
inline constexpr bool is_flag_type(TextBlockFlag) { return true; }
using TextBlockFlags = base::flags<TextBlockFlag>;

[[nodiscard]] style::font WithFlags(
	const style::font &font,
	TextBlockFlags flags,
	style::FontFlags fontFlags = 0);

[[nodiscard]] Qt::LayoutDirection UnpackParagraphDirection(
	bool ltr,
	bool rtl);

struct BlockDescriptor {
	uint16 position = 0;
	TextBlockFlags flags;
	uint16 linkIndex = 0;
	uint16 colorIndex = 0;
};

class AbstractBlock {
public:
	[[nodiscard]] uint16 position() const;
	[[nodiscard]] TextBlockType type() const;
	[[nodiscard]] TextBlockFlags flags() const;
	[[nodiscard]] int objectWidth() const;
	[[nodiscard]] uint16 colorIndex() const;
	[[nodiscard]] uint16 linkIndex() const;
	void setLinkIndex(uint16 index);

protected:
	AbstractBlock(TextBlockType type, BlockDescriptor descriptor);

	uint16 _position = 0;
	uint16 _type : 4 = 0;
	uint16 _flags : 12 = 0;
	uint16 _linkIndex = 0;
	uint16 _colorIndex = 0;

};

class NewlineBlock final : public AbstractBlock {
public:
	NewlineBlock(BlockDescriptor descriptor, uint16 quoteIndex);

	void setQuoteIndex(uint16 index) {
		_quoteIndex = index;
	}
	[[nodiscard]] uint16 quoteIndex() const {
		return _quoteIndex;
	}
	void setParagraphDirection(Qt::LayoutDirection direction) {
		_paragraphLTR = (direction == Qt::LeftToRight);
		_paragraphRTL = (direction == Qt::RightToLeft);
	}
	[[nodiscard]] Qt::LayoutDirection paragraphDirection() const {
		return UnpackParagraphDirection(_paragraphLTR, _paragraphRTL);
	}

private:
	uint16 _quoteIndex = 0;
	bool _paragraphLTR : 1 = false;
	bool _paragraphRTL : 1 = false;

};

class TextBlock final : public AbstractBlock {
public:
	explicit TextBlock(BlockDescriptor descriptor);

};

class EmojiBlock final : public AbstractBlock {
public:
	EmojiBlock(BlockDescriptor descriptor, EmojiPtr emoji);

	[[nodiscard]] EmojiPtr emoji() const {
		return _emoji;
	}

private:
	EmojiPtr _emoji = nullptr;

};

class CustomEmojiBlock final : public AbstractBlock {
public:
	CustomEmojiBlock(
		BlockDescriptor descriptor,
		std::unique_ptr<CustomEmoji> custom);

	[[nodiscard]] not_null<CustomEmoji*> custom() const {
		return _custom.get();
	}

private:
	std::unique_ptr<CustomEmoji> _custom;

};

class SkipBlock final : public AbstractBlock {
public:
	SkipBlock(BlockDescriptor descriptor, int width, int height);

	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;

private:
	int _width = 0;
	int _height = 0;

};

class Block final {
public:
	Block();
	Block(Block &&other);
	Block &operator=(Block &&other);
	~Block();

	[[nodiscard]] static Block Newline(
		BlockDescriptor descriptor,
		uint16 quoteIndex);
	[[nodiscard]] static Block Text(BlockDescriptor descriptor);
	[[nodiscard]] static Block Emoji(
		BlockDescriptor descriptor,
		EmojiPtr emoji);
	[[nodiscard]] static Block CustomEmoji(
		BlockDescriptor descriptor,
		std::unique_ptr<CustomEmoji> custom);

	[[nodiscard]] static Block Skip(
		BlockDescriptor descriptor,
		int width,
		int height);

	template <typename FinalBlock>
	[[nodiscard]] FinalBlock &unsafe() {
		return *reinterpret_cast<FinalBlock*>(&_data);
	}

	template <typename FinalBlock>
	[[nodiscard]] const FinalBlock &unsafe() const {
		return *reinterpret_cast<const FinalBlock*>(&_data);
	}

	[[nodiscard]] AbstractBlock *get();
	[[nodiscard]] const AbstractBlock *get() const;

	[[nodiscard]] AbstractBlock *operator->();
	[[nodiscard]] const AbstractBlock *operator->() const;

	[[nodiscard]] AbstractBlock &operator*();
	[[nodiscard]] const AbstractBlock &operator*() const;

private:
	struct Tag {
	};

	explicit Block(const Tag &) {
	}

	template <typename FinalType, typename ...Args>
	[[nodiscard]] static Block New(Args &&...args) {
		auto result = Block(Tag{});
		result.emplace<FinalType>(std::forward<Args>(args)...);
		return result;
	}

	template <typename FinalType, typename ...Args>
	void emplace(Args &&...args) {
		new (&_data) FinalType(std::forward<Args>(args)...);
	}

	void destroy();

	static_assert(sizeof(NewlineBlock) <= sizeof(SkipBlock));
	static_assert(alignof(NewlineBlock) <= alignof(void*));
	static_assert(sizeof(EmojiBlock) <= sizeof(SkipBlock));
	static_assert(alignof(EmojiBlock) <= alignof(void*));
	static_assert(sizeof(TextBlock) <= sizeof(SkipBlock));
	static_assert(alignof(TextBlock) <= alignof(void*));
	static_assert(sizeof(CustomEmojiBlock) <= sizeof(SkipBlock));
	static_assert(alignof(CustomEmojiBlock) <= alignof(void*));

	std::aligned_storage_t<sizeof(SkipBlock), alignof(void*)> _data;

};

using Blocks = std::vector<Block>;

[[nodiscard]] inline uint16 CountPosition(Blocks::const_iterator i) {
	return (*i)->position();
}

[[nodiscard]] int CountBlockHeight(
	const AbstractBlock *block,
	const style::TextStyle *st);

[[nodiscard]] inline bool IsMono(TextBlockFlags flags) {
	return (flags & TextBlockFlag::Pre) || (flags & TextBlockFlag::Code);
}

} // namespace Ui::Text
