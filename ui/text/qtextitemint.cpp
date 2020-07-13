// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#ifdef DESKTOP_APP_USE_PACKAGED
#include <private/qtextengine_p.h>

QTextItemInt::QTextItemInt(
	const QGlyphLayout &g,
	QFont *font,
	const QChar *chars_,
	int numChars,
	QFontEngine *fe,
	const QTextCharFormat &format)
: flags(0)
, justified(false)
, underlineStyle(QTextCharFormat::NoUnderline)
, charFormat(format)
, num_chars(numChars)
, chars(chars_)
, logClusters(0)
, f(font)
, glyphs(g)
, fontEngine(fe) {
}

void QTextItemInt::initWithScriptItem(const QScriptItem &si) {
	// explicitly initialize flags so that initFontAttributes can be called
	// multiple times on the same TextItem
	flags = 0;
	if (si.analysis.bidiLevel %2)
		flags |= QTextItem::RightToLeft;
	ascent = si.ascent;
	descent = si.descent;

	if (charFormat.hasProperty(QTextFormat::TextUnderlineStyle)) {
		underlineStyle = charFormat.underlineStyle();
	} else if (charFormat.boolProperty(QTextFormat::FontUnderline)
				|| f->d->underline) {
		underlineStyle = QTextCharFormat::SingleUnderline;
	}

	// compat
	if (underlineStyle == QTextCharFormat::SingleUnderline)
		flags |= QTextItem::Underline;

	if (f->d->overline || charFormat.fontOverline())
		flags |= QTextItem::Overline;
	if (f->d->strikeOut || charFormat.fontStrikeOut())
		flags |= QTextItem::StrikeOut;
}
#endif // !DESKTOP_APP_USE_PACKAGED
