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

#include <private/qfixed_p.h>

#include <crl/crl_time.h>

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
	Bold      = 0x001,
	Italic    = 0x002,
	Underline = 0x004,
	StrikeOut = 0x008,
	Tilde     = 0x010, // Tilde fix in OpenSans.
	Semibold  = 0x020,
	Code      = 0x040,
	Pre       = 0x080,
	Spoiler   = 0x100,
};
inline constexpr bool is_flag_type(TextBlockFlag) { return true; }
using TextBlockFlags = base::flags<TextBlockFlag>;

[[nodiscard]] style::font WithFlags(
	const style::font &font,
	TextBlockFlags flags,
	uint32 fontFlags = 0);

class AbstractBlock {
public:
	[[nodiscard]] uint16 position() const;
	[[nodiscard]] TextBlockType type() const;
	[[nodiscard]] TextBlockFlags flags() const;
	[[nodiscard]] uint16 colorIndex() const;
	[[nodiscard]] uint16 linkIndex() const;
	void setLinkIndex(uint16 index);

	[[nodiscard]] QFixed f_width() const;
	[[nodiscard]] QFixed f_rpadding() const;

	// Should be virtual, but optimized throught type() call.
	[[nodiscard]] QFixed f_rbearing() const;

protected:
	AbstractBlock(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockType type,
		uint16 flags,
		uint16 linkIndex,
		uint16 colorIndex);

	uint16 _position = 0;
	uint16 _type : 4 = 0;
	uint16 _flags : 12 = 0;
	uint16 _linkIndex = 0;
	uint16 _colorIndex = 0;

	QFixed _width = 0;

	// Right padding: spaces after the last content of the block (like a word).
	// This holds spaces after the end of the block, for example a text ending
	// with a space before a link has started. If text block has a leading spaces
	// (for example a text block after a link block) it is prepended with an empty
	// word that holds those spaces as a right padding.
	QFixed _rpadding = 0;

};

class NewlineBlock final : public AbstractBlock {
public:
	NewlineBlock(
		const style::font &font,
		const QString &str,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex);

	[[nodiscard]] Qt::LayoutDirection nextDirection() const;

private:
	Qt::LayoutDirection _nextDirection = Qt::LayoutDirectionAuto;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class TextWord final {
public:
	TextWord() = default;
	TextWord(
		uint16 position,
		QFixed width,
		QFixed rbearing,
		QFixed rpadding = 0);

	[[nodiscard]] uint16 position() const;
	[[nodiscard]] QFixed f_rbearing() const;
	[[nodiscard]] QFixed f_width() const;
	[[nodiscard]] QFixed f_rpadding() const;

	void add_rpadding(QFixed padding);

private:
	uint16 _position = 0;
	int16 _rbearing = 0;
	QFixed _width;
	QFixed _rpadding;

};

class TextBlock final : public AbstractBlock {
public:
	TextBlock(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		QFixed minResizeWidth);

private:
	[[nodiscard]] QFixed real_f_rbearing() const;

	QVector<TextWord> _words;

	friend class String;
	friend class Parser;
	friend class Renderer;
	friend class BlockParser;
	friend class AbstractBlock;

};

class EmojiBlock final : public AbstractBlock {
public:
	EmojiBlock(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		EmojiPtr emoji);

private:
	EmojiPtr _emoji = nullptr;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class CustomEmojiBlock final : public AbstractBlock {
public:
	CustomEmojiBlock(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		std::unique_ptr<CustomEmoji> custom);

private:
	std::unique_ptr<CustomEmoji> _custom;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class SkipBlock final : public AbstractBlock {
public:
	SkipBlock(
		const style::font &font,
		const QString &text,
		uint16 position,
		int32 width,
		int32 height,
		uint16 linkIndex,
		uint16 colorIndex);

	[[nodiscard]] int height() const;

private:
	int _height = 0;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class Block final {
public:
	Block();
	Block(Block &&other);
	Block &operator=(Block &&other);
	~Block();

	[[nodiscard]] static Block Newline(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex);

	[[nodiscard]] static Block Text(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		QFixed minResizeWidth);

	[[nodiscard]] static Block Emoji(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		EmojiPtr emoji);

	[[nodiscard]] static Block CustomEmoji(
		const style::font &font,
		const QString &text,
		uint16 position,
		uint16 length,
		TextBlockFlags flags,
		uint16 linkIndex,
		uint16 colorIndex,
		std::unique_ptr<CustomEmoji> custom);

	[[nodiscard]] static Block Skip(
		const style::font &font,
		const QString &text,
		uint16 position,
		int32 width,
		int32 height,
		uint16 linkIndex,
		uint16 colorIndex);

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

	static_assert(sizeof(NewlineBlock) <= sizeof(TextBlock));
	static_assert(alignof(NewlineBlock) <= alignof(void*));
	static_assert(sizeof(EmojiBlock) <= sizeof(TextBlock));
	static_assert(alignof(EmojiBlock) <= alignof(void*));
	static_assert(sizeof(CustomEmojiBlock) <= sizeof(TextBlock));
	static_assert(alignof(CustomEmojiBlock) <= alignof(void*));
	static_assert(sizeof(SkipBlock) <= sizeof(TextBlock));
	static_assert(alignof(SkipBlock) <= alignof(void*));

	std::aligned_storage_t<sizeof(TextBlock), alignof(void*)> _data;

};

[[nodiscard]] int CountBlockHeight(
	const AbstractBlock *block,
	const style::TextStyle *st);

[[nodiscard]] inline bool IsMono(TextBlockFlags flags) {
	return (flags & TextBlockFlag::Pre) || (flags & TextBlockFlag::Code);
}

} // namespace Ui::Text
