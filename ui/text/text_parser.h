// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"

namespace Ui::Text {

class Parser {
public:
	Parser(
		not_null<String*> string,
		const TextWithEntities &textWithEntities,
		const TextParseOptions &options,
		const std::any &context);

private:
	struct ReadyToken {
	};

	class StartedEntity {
	public:
		enum class Type {
			Flags,
			Link,
			IndexedLink,
			Spoiler,
			CustomEmoji,
		};

		explicit StartedEntity(TextBlockFlags flags);
		explicit StartedEntity(uint16 index, Type type);

		[[nodiscard]] Type type() const;
		[[nodiscard]] std::optional<TextBlockFlags> flags() const;
		[[nodiscard]] std::optional<uint16> lnkIndex() const;
		[[nodiscard]] std::optional<uint16> spoilerIndex() const;

	private:
		const int _value = 0;
		const Type _type;

	};

	Parser(
		not_null<String*> string,
		TextWithEntities &&source,
		const TextParseOptions &options,
		const std::any &context,
		ReadyToken);

	void trimSourceRange();
	void blockCreated();
	void createBlock(int32 skipBack = 0);
	void createNewlineBlock();

	// Returns true if at least one entity was parsed in the current position.
	bool checkEntities();
	void parseCurrentChar();
	void parseEmojiFromCurrent();
	void finalize(const TextParseOptions &options);

	void finishEntities();
	void skipPassedEntities();
	void skipBadEntities();

	bool isInvalidEntity(const EntityInText &entity) const;
	bool isLinkEntity(const EntityInText &entity) const;

	bool processCustomIndex(uint16 index);

	void parse(const TextParseOptions &options);
	void computeLinkText(
		const QString &linkData,
		QString *outLinkText,
		EntityLinkShown *outShown);

	const not_null<String*> _t;
	const TextWithEntities _source;
	const std::any &_context;
	const QChar * const _start = nullptr;
	const QChar *_end = nullptr; // mutable, because we trim by decrementing.
	const QChar *_ptr = nullptr;
	const EntitiesInText::const_iterator _entitiesEnd;
	EntitiesInText::const_iterator _waitingEntity;
	QString _customEmojiData;
	const bool _multiline = false;

	const QFixed _stopAfterWidth; // summary width of all added words
	const bool _checkTilde = false; // do we need a special text block for tilde symbol

	std::vector<uint16> _linksIndexes;

	std::vector<EntityLinkData> _links;
	std::vector<EntityLinkData> _spoilers;
	std::vector<EntityLinkData> _monos;
	base::flat_map<
		const QChar*,
		std::vector<StartedEntity>> _startedEntities;

	uint16 _maxLnkIndex = 0;
	uint16 _maxShiftedLnkIndex = 0;

	// current state
	int32 _flags = 0;
	uint16 _lnkIndex = 0;
	uint16 _spoilerIndex = 0;
	uint16 _monoIndex = 0;
	EmojiPtr _emoji = nullptr; // current emoji, if current word is an emoji, or zero
	int32 _blockStart = 0; // offset in result, from which current parsed block is started
	int32 _diacs = 0; // diac chars skipped without good char
	QFixed _sumWidth;
	bool _sumFinished = false;
	bool _newlineAwaited = false;

	// current char data
	QChar _ch; // current char (low surrogate, if current char is surrogate pair)
	int32 _emojiLookback = 0; // how far behind the current ptr to look for current emoji
	bool _lastSkipped = false; // did we skip current char

};

} // namespace Ui::Text
