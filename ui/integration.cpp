// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/integration.h"

#include "ui/gl/gl_detection.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_block.h"
#include "ui/toast/toast.h"
#include "ui/basic_click_handlers.h"
#include "base/platform/base_platform_info.h"

namespace Ui {
namespace {

Integration *IntegrationInstance = nullptr;

} // namespace

void Integration::Set(not_null<Integration*> instance) {
	IntegrationInstance = instance;

#ifdef DESKTOP_APP_USE_ANGLE
	GL::ConfigureANGLE();
#endif
}

Integration &Integration::Instance() {
	Expects(IntegrationInstance != nullptr);

	return *IntegrationInstance;
}

bool Integration::Exists() {
	return (IntegrationInstance != nullptr);
}

void Integration::textActionsUpdated() {
}

void Integration::activationFromTopPanel() {
}

bool Integration::screenIsLocked() {
	return false;
}

std::shared_ptr<ClickHandler> Integration::createLinkHandler(
		const EntityLinkData &data,
		const std::any &context) {
	switch (data.type) {
	case EntityType::CustomUrl:
		return !data.data.isEmpty()
			? std::make_shared<UrlClickHandler>(data.data, false)
			: nullptr;
	case EntityType::Email:
	case EntityType::Url:
		return !data.data.isEmpty()
			? std::make_shared<UrlClickHandler>(
				data.data,
				data.shown == EntityLinkShown::Full)
			: nullptr;
	}
	return nullptr;
}

std::unique_ptr<Text::CustomEmoji> Integration::createCustomEmoji(
		QStringView data,
		const std::any &context) {
	return nullptr;
}

Fn<void()> Integration::createSpoilerRepaint(const std::any &context) {
	return nullptr;
}

bool Integration::allowClickHandlerActivation(
		const std::shared_ptr<ClickHandler> &handler,
		const ClickContext &context) {
	return true;
}

bool Integration::handleUrlClick(
		const QString &url,
		const QVariant &context) {
	return false;
}

bool Integration::copyPreOnClick(const QVariant &context) {
	Toast::Show(u"Code copied to clipboard."_q);
	return true;
}

QString Integration::convertTagToMimeTag(const QString &tagId) {
	return tagId;
}

const Emoji::One *Integration::defaultEmojiVariant(const Emoji::One *emoji) {
	return emoji;
}

rpl::producer<> Integration::forcePopupMenuHideRequests() {
	return rpl::never<rpl::empty_value>();
}

QString Integration::phraseContextCopyText() {
	return "Copy text";
}

QString Integration::phraseContextCopyEmail() {
	return "Copy email";
}

QString Integration::phraseContextCopyLink() {
	return "Copy link";
}

QString Integration::phraseContextCopySelected() {
	return "Copy to clipboard";
}

QString Integration::phraseFormattingTitle() {
	return "Formatting";
}

QString Integration::phraseFormattingLinkCreate() {
	return "Create link";
}

QString Integration::phraseFormattingLinkEdit() {
	return "Edit link";
}

QString Integration::phraseFormattingClear() {
	return "Plain text";
}

QString Integration::phraseFormattingBold() {
	return "Bold";
}

QString Integration::phraseFormattingItalic() {
	return "Italic";
}

QString Integration::phraseFormattingUnderline() {
	return "Underline";
}

QString Integration::phraseFormattingStrikeOut() {
	return "Strike-through";
}

QString Integration::phraseFormattingBlockquote() {
	return "Quote";
}

QString Integration::phraseFormattingMonospace() {
	return "Monospace";
}

QString Integration::phraseFormattingSpoiler() {
	return "Spoiler";
}

QString Integration::phraseButtonOk() {
	return "OK";
}

QString Integration::phraseButtonClose() {
	return "Close";
}

QString Integration::phraseButtonCancel() {
	return "Cancel";
}

QString Integration::phrasePanelCloseWarning() {
	return "Warning";
}

QString Integration::phrasePanelCloseUnsaved() {
	return "Changes that you made may not be saved.";
}

QString Integration::phrasePanelCloseAnyway() {
	return "Close anyway";
}

QString Integration::phraseBotSharePhone() {
	return "Do you want to share your phone number with this bot?";
}

QString Integration::phraseBotSharePhoneTitle() {
	return "Phone number";
}

QString Integration::phraseBotSharePhoneConfirm() {
	return "Share";
}

QString Integration::phraseBotAllowWrite() {
	return "Do you want to allow this bot to write you?";
}

QString Integration::phraseBotAllowWriteTitle() {
	return "Allow write";
}

QString Integration::phraseBotAllowWriteConfirm() {
	return "Allow";
}

QString Integration::phraseQuoteHeaderCopy() {
	return "copy";
}

} // namespace Ui
