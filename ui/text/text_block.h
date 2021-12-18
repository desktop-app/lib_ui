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
		uchar flags,
		uint16 lnkIndex,
		uint16 spoilerIndex);

	uint16 _from = 0;

	uint32 _flags = 0; // 4 bits empty, 16 bits lnkIndex, 4 bits type, 8 bits flags

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
		uchar flags,
		uint16 lnkIndex,
		uint16 spoilerIndex)
	: AbstractBlock(font, str, from, length, flags, lnkIndex, spoilerIndex) {
		_flags |= ((TextBlockTNewline & 0x0F) << 8);
	}

	Qt::LayoutDirection nextDirection() const {
		return _nextDir;
	}

private:
	Qt::LayoutDirection _nextDir = Qt::LayoutDirectionAuto;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class TextWord final {
public:
	TextWord() = default;
	TextWord(uint16 from, QFixed width, QFixed rbearing, QFixed rpadding = 0)
	: _from(from)
	, _rbearing((rbearing.value() > 0x7FFF)
		? 0x7FFF
		: (rbearing.value() < -0x7FFF ? -0x7FFF : rbearing.value()))
	, _width(width)
	, _rpadding(rpadding) {
	}
	uint16 from() const {
		return _from;
	}
	QFixed f_rbearing() const {
		return QFixed::fromFixed(_rbearing);
	}
	QFixed f_width() const {
		return _width;
	}
	QFixed f_rpadding() const {
		return _rpadding;
	}
	void add_rpadding(QFixed padding) {
		_rpadding += padding;
	}

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
		uchar flags,
		uint16 lnkIndex,
		uint16 spoilerIndex);

private:
	QFixed real_f_rbearing() const {
		return _words.isEmpty() ? 0 : _words.back().f_rbearing();
	}

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
		uchar flags,
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
		uint16 spoilerIndex)
	: AbstractBlock(font, str, from, 1, 0, lnkIndex, spoilerIndex)
	, _height(h) {
		_flags |= ((TextBlockTSkip & 0x0F) << 8);
		_width = w;
	}

	int height() const {
		return _height;
	}

private:
	int _height = 0;

	friend class String;
	friend class Parser;
	friend class Renderer;

};

class Block final {
public:
	Block() {
		Unexpected("Should not be called.");
	}
	Block(const Block &other) {
		switch (other->type()) {
		case TextBlockTNewline:
			emplace<NewlineBlock>(other.unsafe<NewlineBlock>());
			break;
		case TextBlockTText:
			emplace<TextBlock>(other.unsafe<TextBlock>());
			break;
		case TextBlockTEmoji:
			emplace<EmojiBlock>(other.unsafe<EmojiBlock>());
			break;
		case TextBlockTSkip:
			emplace<SkipBlock>(other.unsafe<SkipBlock>());
			break;
		default:
			Unexpected("Bad text block type in Block(const Block&).");
		}
	}
	Block(Block &&other) {
		switch (other->type()) {
		case TextBlockTNewline:
			emplace<NewlineBlock>(std::move(other.unsafe<NewlineBlock>()));
			break;
		case TextBlockTText:
			emplace<TextBlock>(std::move(other.unsafe<TextBlock>()));
			break;
		case TextBlockTEmoji:
			emplace<EmojiBlock>(std::move(other.unsafe<EmojiBlock>()));
			break;
		case TextBlockTSkip:
			emplace<SkipBlock>(std::move(other.unsafe<SkipBlock>()));
			break;
		default:
			Unexpected("Bad text block type in Block(Block&&).");
		}
	}
	Block &operator=(const Block &other) {
		if (&other == this) {
			return *this;
		}
		destroy();
		switch (other->type()) {
		case TextBlockTNewline:
			emplace<NewlineBlock>(other.unsafe<NewlineBlock>());
			break;
		case TextBlockTText:
			emplace<TextBlock>(other.unsafe<TextBlock>());
			break;
		case TextBlockTEmoji:
			emplace<EmojiBlock>(other.unsafe<EmojiBlock>());
			break;
		case TextBlockTSkip:
			emplace<SkipBlock>(other.unsafe<SkipBlock>());
			break;
		default:
			Unexpected("Bad text block type in operator=(const Block&).");
		}
		return *this;
	}
	Block &operator=(Block &&other) {
		if (&other == this) {
			return *this;
		}
		destroy();
		switch (other->type()) {
		case TextBlockTNewline:
			emplace<NewlineBlock>(std::move(other.unsafe<NewlineBlock>()));
			break;
		case TextBlockTText:
			emplace<TextBlock>(std::move(other.unsafe<TextBlock>()));
			break;
		case TextBlockTEmoji:
			emplace<EmojiBlock>(std::move(other.unsafe<EmojiBlock>()));
			break;
		case TextBlockTSkip:
			emplace<SkipBlock>(std::move(other.unsafe<SkipBlock>()));
			break;
		default:
			Unexpected("Bad text block type in operator=(Block&&).");
		}
		return *this;
	}
	~Block() {
		destroy();
	}

	[[nodiscard]] static Block Newline(
			const style::font &font,
			const QString &str,
			uint16 from,
			uint16 length,
			uchar flags,
			uint16 lnkIndex,
			uint16 spoilerIndex) {
		return New<NewlineBlock>(font, str, from, length, flags, lnkIndex, spoilerIndex);
	}

	[[nodiscard]] static Block Text(
			const style::font &font,
			const QString &str,
			QFixed minResizeWidth,
			uint16 from,
			uint16 length,
			uchar flags,
			uint16 lnkIndex,
			uint16 spoilerIndex) {
		return New<TextBlock>(
			font,
			str,
			minResizeWidth,
			from,
			length,
			flags,
			lnkIndex,
			spoilerIndex);
	}

	[[nodiscard]] static Block Emoji(
			const style::font &font,
			const QString &str,
			uint16 from,
			uint16 length,
			uchar flags,
			uint16 lnkIndex,
			uint16 spoilerIndex,
			EmojiPtr emoji) {
		return New<EmojiBlock>(
			font,
			str,
			from,
			length,
			flags,
			lnkIndex,
			spoilerIndex,
			emoji);
	}

	[[nodiscard]] static Block Skip(
			const style::font &font,
			const QString &str,
			uint16 from,
			int32 w,
			int32 h,
			uint16 lnkIndex,
			uint16 spoilerIndex) {
		return New<SkipBlock>(font, str, from, w, h, lnkIndex, spoilerIndex);
	}

	template <typename FinalBlock>
	[[nodiscard]] FinalBlock &unsafe() {
		return *reinterpret_cast<FinalBlock*>(&_data);
	}

	template <typename FinalBlock>
	[[nodiscard]] const FinalBlock &unsafe() const {
		return *reinterpret_cast<const FinalBlock*>(&_data);
	}

	[[nodiscard]] AbstractBlock *get() {
		return &unsafe<AbstractBlock>();
	}

	[[nodiscard]] const AbstractBlock *get() const {
		return &unsafe<AbstractBlock>();
	}

	[[nodiscard]] AbstractBlock *operator->() {
		return get();
	}

	[[nodiscard]] const AbstractBlock *operator->() const {
		return get();
	}

	[[nodiscard]] AbstractBlock &operator*() {
		return *get();
	}

	[[nodiscard]] const AbstractBlock &operator*() const {
		return *get();
	}

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

	void destroy() {
		switch (get()->type()) {
		case TextBlockTNewline:
			unsafe<NewlineBlock>().~NewlineBlock();
			break;
		case TextBlockTText:
			unsafe<TextBlock>().~TextBlock();
			break;
		case TextBlockTEmoji:
			unsafe<EmojiBlock>().~EmojiBlock();
			break;
		case TextBlockTSkip:
			unsafe<SkipBlock>().~SkipBlock();
			break;
		default:
			Unexpected("Bad text block type in Block(Block&&).");
		}
	}

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
