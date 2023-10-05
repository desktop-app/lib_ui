// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

#include <rpl/producer.h>

#include <any>

// Methods that must be implemented outside lib_ui.

class QString;
class QWidget;
class QVariant;

struct TextParseOptions;
class ClickHandler;
struct ClickContext;
struct EntityLinkData;

namespace Ui {
namespace Emoji {
class One;
} // namespace Emoji

namespace Text {
class CustomEmoji;
} // namespace Text

class Integration {
public:
	static void Set(not_null<Integration*> instance);
	static Integration &Instance();
	static bool Exists();

	virtual void postponeCall(FnMut<void()> &&callable) = 0;
	virtual void registerLeaveSubscription(not_null<QWidget*> widget) = 0;
	virtual void unregisterLeaveSubscription(not_null<QWidget*> widget) = 0;

	[[nodiscard]] virtual QString emojiCacheFolder() = 0;
	[[nodiscard]] virtual QString openglCheckFilePath() = 0;
	[[nodiscard]] virtual QString angleBackendFilePath() = 0;

	virtual void textActionsUpdated();
	virtual void activationFromTopPanel();

	[[nodiscard]] virtual bool screenIsLocked();

	[[nodiscard]] virtual std::shared_ptr<ClickHandler> createLinkHandler(
		const EntityLinkData &data,
		const std::any &context);
	[[nodiscard]] virtual bool handleUrlClick(
		const QString &url,
		const QVariant &context);
	[[nodiscard]] virtual QString convertTagToMimeTag(const QString &tagId);
	[[nodiscard]] virtual const Emoji::One *defaultEmojiVariant(
		const Emoji::One *emoji);
	[[nodiscard]] virtual auto createCustomEmoji(
		const QString &data,
		const std::any &context) -> std::unique_ptr<Text::CustomEmoji>;
	[[nodiscard]] virtual Fn<void()> createSpoilerRepaint(
		const std::any &context);
	[[nodiscard]] virtual bool allowClickHandlerActivation(
		const std::shared_ptr<ClickHandler> &handler,
		const ClickContext &context);

	[[nodiscard]] virtual rpl::producer<> forcePopupMenuHideRequests();

	[[nodiscard]] virtual QString phraseContextCopyText();
	[[nodiscard]] virtual QString phraseContextCopyEmail();
	[[nodiscard]] virtual QString phraseContextCopyLink();
	[[nodiscard]] virtual QString phraseContextCopySelected();
	[[nodiscard]] virtual QString phraseFormattingTitle();
	[[nodiscard]] virtual QString phraseFormattingLinkCreate();
	[[nodiscard]] virtual QString phraseFormattingLinkEdit();
	[[nodiscard]] virtual QString phraseFormattingClear();
	[[nodiscard]] virtual QString phraseFormattingBold();
	[[nodiscard]] virtual QString phraseFormattingItalic();
	[[nodiscard]] virtual QString phraseFormattingUnderline();
	[[nodiscard]] virtual QString phraseFormattingStrikeOut();
	[[nodiscard]] virtual QString phraseFormattingBlockquote();
	[[nodiscard]] virtual QString phraseFormattingMonospace();
	[[nodiscard]] virtual QString phraseFormattingSpoiler();
	[[nodiscard]] virtual QString phraseButtonOk();
	[[nodiscard]] virtual QString phraseButtonClose();
	[[nodiscard]] virtual QString phraseButtonCancel();
	[[nodiscard]] virtual QString phrasePanelCloseWarning();
	[[nodiscard]] virtual QString phrasePanelCloseUnsaved();
	[[nodiscard]] virtual QString phrasePanelCloseAnyway();
	[[nodiscard]] virtual QString phraseBotSharePhone();
	[[nodiscard]] virtual QString phraseBotSharePhoneTitle();
	[[nodiscard]] virtual QString phraseBotSharePhoneConfirm();
	[[nodiscard]] virtual QString phraseBotAllowWrite();
	[[nodiscard]] virtual QString phraseBotAllowWriteTitle();
	[[nodiscard]] virtual QString phraseBotAllowWriteConfirm();

};

} // namespace Ui
