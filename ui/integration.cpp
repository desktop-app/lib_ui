// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/integration.h"

#include "ui/text/text_entity.h"
#include "ui/basic_click_handlers.h"

namespace Ui {
namespace {

Integration *IntegrationInstance = nullptr;

} // namespace

void Integration::Set(not_null<Integration*> instance) {
	IntegrationInstance = instance;
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

QString Integration::timeFormat() {
	return u"hh:mm"_q;
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

bool Integration::handleUrlClick(
		const QString &url,
		const QVariant &context) {
	return false;
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

QString Integration::phraseFormattingMonospace() {
	return "Monospace";
}

} // namespace Ui
