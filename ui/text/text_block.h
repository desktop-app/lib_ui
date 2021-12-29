// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core.h"
#include "ui/emoji_config.h"

#include <private/qfixed_p.h>

namespace Ui {
namespace Text {

enum TextBlockType {
	TextBlockTNewline = 0x01,
	TextBlockTText = 0x02,
	TextBlockTEmoji = 0x03,
	TextBlockTSkip = 0x04,
};

enum TextBlockFlags {
	TextBlockFBold = 0x01,
	TextBlockFItalic = 0x02,
	TextBlockFUnderline = 0x04,
	TextBlockFStrikeOut = 0x08,
	TextBlockFTilde = 0x10, // tilde fix in OpenSans
	TextBlockFSemibold = 0x20,
	TextBlockFCode = 0x40,
	TextBlockFPre = 0x80,
	TextBlockFPlainLink = 0x100,
};

class AbstractBlock {
public:
	uint16 from() const;
	int width() const;
	int rpadding() const;
	QFixed f_width() const;
	QFixed f_rpadding() const;

	// Should be virtual, but optimized throught type() call.
	QFixed f_rbearing() const;

	uint16 lnkIndex() const;
	void setLnkIndex(uint16 lnkIndex);

	uint16 spoilerIndex() const;
	void setSpoilerIndex(uint16 spoilerIndex);

	TextBlockType type() const;
	int32 flags() const;

protected:
	AbstractBlock(
		const style::font &font,
		const QString &str,
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex);

	uint16 _from = 0;

	uint32 _flags = 0; // 2 bits empty, 16 bits lnkIndex, 4 bits type, 10 bits flags

	uint16 _spoilerIndex = 0;

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
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex);

	Qt::LayoutDirection nextDirection() const;

private:
	Qt::LayoutDirection _nextDir = Qt::LayoutDirectionAuto;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class TextWord final {
public:
	TextWord() = default;
	TextWord(uint16 from, QFixed width, QFixed rbearing, QFixed rpadding = 0);
	uint16 from() const;
	QFixed f_rbearing() const;
	QFixed f_width() const;
	QFixed f_rpadding() const;
	void add_rpadding(QFixed padding);

private:
	uint16 _from = 0;
	int16 _rbearing = 0;
	QFixed _width, _rpadding;

};

class TextBlock final : public AbstractBlock {
public:
	TextBlock(
		const style::font &font,
		const QString &str,
		QFixed minResizeWidth,
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex);

private:
	QFixed real_f_rbearing() const;

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
		const QString &str,
		uint16 from,
		uint16 length,
		uint16 flags,
		uint16 lnkIndex,
		uint16 spoilerIndex,
		EmojiPtr emoji);

private:
	EmojiPtr _emoji = nullptr;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class SkipBlock final : public AbstractBlock {
public:
	SkipBlock(
		const style::font &font,
		const QString &str,
		uint16 from,
		int32 w,
		int32 h,
		uint16 lnkIndex,
		uint16 spoilerIndex);

	int height() const;

private:
	int _height = 0;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class Block final {
public:
	Block();
	Block(const Block &other);
	Block(Block &&other);
	Block &operator=(const Block &other);
	Block &operator=(Block &&other);
	~Block();

	[[nodiscard]] static Block Newline(
			const style::font &font,
			const QString &str,
			uint16 from,
			uint16 length,
			uint16 flags,
			uint16 lnkIndex,
			uint16 spoilerIndex);

	[[nodiscard]] static Block Text(
			const style::font &font,
			const QString &str,
			QFixed minResizeWidth,
			uint16 from,
			uint16 length,
			uint16 flags,
			uint16 lnkIndex,
			uint16 spoilerIndex);

	[[nodiscard]] static Block Emoji(
			const style::font &font,
			const QString &str,
			uint16 from,
			uint16 length,
			uint16 flags,
			uint16 lnkIndex,
			uint16 spoilerIndex,
			EmojiPtr emoji);

	[[nodiscard]] static Block Skip(
			const style::font &font,
			const QString &str,
			uint16 from,
			int32 w,
			int32 h,
			uint16 lnkIndex,
			uint16 spoilerIndex);

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
	static_assert(sizeof(SkipBlock) <= sizeof(TextBlock));
	static_assert(alignof(SkipBlock) <= alignof(void*));

	std::aligned_storage_t<sizeof(TextBlock), alignof(void*)> _data;

};

} // namespace Text
} // namespace Ui
