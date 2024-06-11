// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QTextObjectInterface>

#include "ui/text/text.h"

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui {

class InputField;

class CustomFieldObject : public QObject, public QTextObjectInterface {
public:
	using Factory = Fn<std::unique_ptr<Text::CustomEmoji>(QStringView)>;

	CustomFieldObject(
		not_null<InputField*> field,
		Factory factory,
		Fn<bool()> paused);
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
	[[nodiscard]] TextWithTags collapsedText(int quoteId) const;

	void setNow(crl::time now);
	void clear();

private:
	struct Quote {
		TextWithTags text;
		Text::String string;
	};

	const not_null<InputField*> _field;
	Factory _factory;
	Fn<bool()> _paused;
	base::flat_map<uint64, std::unique_ptr<Text::CustomEmoji>> _emoji;
	base::flat_map<int, Quote> _quotes;
	crl::time _now = 0;
	int _skip = 0;

};

} // namespace Ui
