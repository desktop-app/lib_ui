// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QTextObjectInterface>

#include "ui/text/text.h"
#include "ui/text/text_custom_emoji.h"

namespace Ui {

class InputField;

class CustomFieldObject : public QObject, public QTextObjectInterface {
public:
	CustomFieldObject(
		not_null<InputField*> field,
		Fn<std::any(Fn<void()> repaint)> context,
		Fn<bool()> pausedEmoji,
		Fn<bool()> pausedSpoiler,
		Text::CustomEmojiFactory factory);
	~CustomFieldObject();

	void *qt_metacast(const char *iid) override;

	QSizeF intrinsicSize(
		QTextDocument *doc,
		int posInDocument,
		const QTextFormat &format) override;
	void drawObject(
		QPainter *painter,
		const QRectF &rect,
		QTextDocument *doc,
		int posInDocument,
		const QTextFormat &format) override;

	void setCollapsedText(int quoteId, TextWithTags text);
	[[nodiscard]] const TextWithTags &collapsedText(int quoteId) const;

	void setNow(crl::time now);

	void clearEmoji();
	void clearQuotes();

private:
	struct Quote {
		TextWithTags text;
		Text::String string;
	};

	using Factory = Fn<std::unique_ptr<Text::CustomEmoji>(QStringView)>;
	[[nodiscard]] Factory makeFactory(
		Text::CustomEmojiFactory custom = nullptr);

	const not_null<InputField*> _field;
	const Fn<std::any(Fn<void()> repaint)> _context;
	const Fn<bool()> _pausedEmoji;
	const Fn<bool()> _pausedSpoiler;
	const Factory _factory;

	base::flat_map<uint64, std::unique_ptr<Text::CustomEmoji>> _emoji;
	base::flat_map<int, Quote> _quotes;
	crl::time _now = 0;
	int _skip = 0;

};

} // namespace Ui
