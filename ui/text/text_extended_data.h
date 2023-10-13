// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/spoiler_mess.h"
#include "ui/effects/animations.h"
#include "ui/click_handler.h"

namespace Ui::Text {

struct Modification;
class String;

class SpoilerClickHandler final : public ClickHandler {
public:
	SpoilerClickHandler(
		not_null<String*> text,
		Fn<bool(const ClickContext&)> filter);

	[[nodiscard]] not_null<String*> text() const;
	void setText(not_null<String*> text);

	void onClick(ClickContext context) const override;

private:
	not_null<String*> _text;
	const Fn<bool(const ClickContext &)> _filter;

};

struct SpoilerData {
	explicit SpoilerData(Fn<void()> repaint)
	: animation(std::move(repaint)) {
	}

	SpoilerAnimation animation;
	std::shared_ptr<SpoilerClickHandler> link;
	Animations::Simple revealAnimation;
	bool revealed = false;
};

struct ParagraphDetails {
	QString language;
	ClickHandlerPtr copy;
	int copyWidth = 0;
	int maxWidth = 0;
	int minHeight = 0;
	int scrollLeft = 0;
	bool blockquote = false;
	bool pre = false;
};

struct ExtendedData {
	std::vector<ClickHandlerPtr> links;
	std::vector<ParagraphDetails> paragraphs;
	std::unique_ptr<SpoilerData> spoiler;
	std::vector<Modification> modifications;
};

} // namespace Ui::Text
