// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_shaper.h"

#include "ui/text/text_block.h"
#include "ui/style/style_core.h"
#include "styles/style_basic.h"

#include <pango/pangocairo.h>

// Only the backend of Pango over fontconfig tells what a font was hinted with,
// and it is the only one built here - see SupportsSubpixelPositions() below.
#if __has_include(<pango/pangofc-font.h>)
#include <pango/pangofc-font.h>
#define LIB_UI_PANGO_OVER_FONTCONFIG

// Where fontconfig is what a font is matched by, the desktop is what the
// settings of its rasterization are read from - see SystemFontOptions().
#include "ui/platform/linux/ui_font_settings_linux.h"
#endif // __has_include(<pango/pangofc-font.h>)

#if !PANGO_VERSION_CHECK(1, 48, 0) && __has_include(<dlfcn.h>)
// Only to ask the loader for what these headers are too old to declare.
#include <dlfcn.h>
#endif // Pango < 1.48.0 && __has_include(<dlfcn.h>)

#include <QtCore/QtMath>
#include <QtCore/QTextBoundaryFinder>
#include <QtGui/QPainter>
#include <QtGui/QPaintEngine>
#include <QtGui/QBackingStore>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace Ui::Text {
namespace {

// Pango is asked for everything in pixels of the device, so that hinting and
// rasterization happen on the grid the screen actually has - which is what Qt
// gives up when it scales instead. The engine outside stays in logical units,
// so the ratio is taken out again here.
//
// Pango counts in 1/1024 of a pixel, this engine in 1/64 of one.
[[nodiscard]] Fixed FromPango(int units, qreal ratio) {
	return Fixed::FromRaw(qRound(units / ((PANGO_SCALE / 64) * ratio)));
}

[[nodiscard]] int ToPango(Fixed value, qreal ratio) {
	return qRound(value.raw() * (PANGO_SCALE / 64) * ratio);
}

} // namespace

namespace {

// Pango indexes text by byte in UTF-8, the engine by QChar in UTF-16, and the
// two only agree below U+0080. Everything crossing the boundary goes through
// this, so that neither side has to know about the other's units.
class Text final {
public:
	explicit Text(QStringView text);

	[[nodiscard]] const char *data() const {
		return _utf8.constData();
	}
	[[nodiscard]] int size() const {
		return int(_utf8.size());
	}

	[[nodiscard]] int toUtf8(int position) const {
		return _toUtf8[position];
	}
	[[nodiscard]] int toUtf16(int byte) const {
		return _toUtf16[byte];
	}
	[[nodiscard]] const QString &utf16() const {
		return _utf16;
	}

private:
	QString _utf16;
	QByteArray _utf8;

	// One more than the length in each, so that the end maps to the end.
	std::vector<int> _toUtf8;
	std::vector<int> _toUtf16;

};

Text::Text(QStringView text) {
	_utf16 = text.toString();
	_utf8 = text.toUtf8();
	_toUtf8.resize(text.size() + 1);
	_toUtf16.resize(_utf8.size() + 1);

	auto byte = 0;
	for (auto i = 0, count = int(text.size()); i != count; ++i) {
		const auto ch = text.at(i);
		if (ch.isLowSurrogate()) {
			// Counted already, with the high surrogate that came before it,
			// and pointed at the start of the pair: nothing may be cut apart
			// between the two, and a cut that lands here must not reach past
			// what the pair takes.
			_toUtf8[i] = (i > 0) ? _toUtf8[i - 1] : byte;
			continue;
		}
		_toUtf8[i] = byte;
		const auto length = ch.isHighSurrogate()
			? 4
			: (ch.unicode() < 0x80)
			? 1
			: (ch.unicode() < 0x800)
			? 2
			: 3;
		for (auto j = 0; j != length; ++j) {
			_toUtf16[byte + j] = i;
		}
		byte += length;
	}
	_toUtf8[text.size()] = byte;
	_toUtf16[_utf8.size()] = int(text.size());
}

// Only what shaping needs: the decorations are drawn by the painter, not
// baked into the glyphs.
//
// Only what the fonts of the styles can carry, as well. They are made by
// ResolveFont() in style_core_font.cpp out of a family, a size and the flags
// of a style, and a font of one is the only way text reaches this backend -
// so what no style can ask for is not answered here: the width of the face,
// the hint of a family, the strategy of matching, the preference of hinting,
// the spacing of letters and of words, kerning, features, variable axes and
// the line above the text.
[[nodiscard]] PangoFontDescription *FontDescription(
		const QFont &font,
		qreal ratio) {
	const auto result = pango_font_description_new();

	// Only what was really asked for. Both sides keep a mask of the fields
	// that were set - Qt to know what to inherit, Pango to know what to match
	// against - and saying a field with the value it has by default is not
	// the same as leaving it alone: it turns into a demand of the matching.
	// Asked about below only where the value of a field that was never set
	// would still be worth saying; everywhere else the default says nothing
	// by itself, and an empty list stays an empty list.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const auto resolved = font.resolveMask();
#else // Qt >= 6.0.0
	const auto resolved = font.resolve();
#endif // Qt < 6.0.0

	// What was asked for, not what Qt resolved it to: resolving costs a lookup
	// of the font, and Pango does a lookup of its own from the name either way
	// - the two agree as long as both are looking at the same list of fonts,
	// which is what registering the ones of the application is for. Measured
	// on the goldens: the two give the same layout, file for file.

	// What Qt was told to fall back to, said the way Pango takes it: a list of
	// families, walked in order for every character none of the earlier ones
	// has a glyph for. Qt keeps the same list against the name that was asked
	// for, and applies it in the engine of its own, which is not in play here.
	auto families = QStringList();

	// All of them and not only the first: a font may name several, and what
	// Qt calls the family of it is the first of that list. Repeats are left
	// in - Pango takes the first family that has the letter, so a name said
	// twice can not change the answer, while looking for repeats would cost
	// more than they do.
	for (const auto &family : font.families()) {
		if (family.isEmpty()) {
			continue;
		}
		families.push_back(family);
		for (const auto &substitute : QFont::substitutes(family)) {
			// Empty just as much: what Qt keeps as a substitution is only
			// lowercased on the way in, never looked at.
			if (!substitute.isEmpty()) {
				families.push_back(substitute);
			}
		}
	}

	if (!families.isEmpty()) {
		pango_font_description_set_family(
			result,
			families.join(',').toUtf8().constData());
	}
	// Sizes go in as pixels of the device wherever there is one to give, so
	// that hinting and rasterization happen on the grid the screen has. A
	// size in points is left to Pango instead: it turns them into the units
	// of the device at the resolution it was built with, which is the same
	// base dpi Qt interprets a point size against. The scale of the device
	// is no part of that and Pango knows nothing about it, so it goes into
	// the size itself either way.
	if (!(resolved & QFont::SizeResolved)) {
	} else if (font.pixelSize() > 0) {
		pango_font_description_set_absolute_size(
			result,
			font.pixelSize() * double(PANGO_SCALE) * ratio);
	} else {
		pango_font_description_set_size(
			result,
			qRound(font.pointSizeF() * PANGO_SCALE * ratio));
	}
	if (resolved & QFont::WeightResolved) {
		pango_font_description_set_weight(
			result,
			PangoWeight(font.weight()));
	}
	if (resolved & QFont::StyleResolved) {
		pango_font_description_set_style(
			result,
			(font.style() == QFont::StyleOblique)
			? PANGO_STYLE_OBLIQUE
			: font.italic()
			? PANGO_STYLE_ITALIC
			: PANGO_STYLE_NORMAL);
	}

	// Nothing is said about capitalization, small capitals with it: Qt applies
	// all of it in the itemizer of its own engine, and the backend over the
	// private API does not go through that one either - it itemizes by itself.
	// So neither of them has ever done it, and text that has to be in capitals
	// is put in capitals before it ever gets here.
	return result;
}



// The order the items are drawn in, which is not the order they are in when
// the line mixes directions - rule L2 of the bidi algorithm, over the levels
// the items came out with.
void ReorderVisually(
		const std::vector<Item> &items,
		std::vector<int> &order) {
	const auto count = int(items.size());
	auto highest = uchar(0);
	auto lowestOdd = uchar(63);
	for (auto i = 0; i != count; ++i) {
		order[i] = i;
		const auto level = items[i].bidiLevel;
		highest = std::max(highest, level);
		if (level % 2) {
			lowestOdd = std::min(lowestOdd, level);
		}
	}
	for (auto level = highest; level >= lowestOdd && level > 0; --level) {
		for (auto i = 0; i != count; ++i) {
			if (items[order[i]].bidiLevel < level) {
				continue;
			}
			auto till = i;
			while (till + 1 != count
				&& items[order[till + 1]].bidiLevel >= level) {
				++till;
			}
			std::reverse(begin(order) + i, begin(order) + till + 1);
			i = till;
		}
	}
}

// What the desktop was told about drawing text, said the way GTK says it - and
// inside a sandbox it is the only place where it is said at all, because the
// fontconfig there belongs to the sandbox and knows nothing of the system.
//
// Only what was actually answered is put here. Everything left alone still
// comes from the pattern fontconfig matched for the font, because cairo merges
// the two and takes the pattern for every field these options leave at its
// default - _cairo_ft_options_merge() in cairo-ft-font.c, where the options
// handed in are the ones accumulated into. So rules written for a family or
// for a range of sizes keep applying to whatever the desktop did not name.
// Asked at the one place the answer is needed: the context every font is
// loaded through carries it from there on.
[[nodiscard]] cairo_font_options_t *MakeSystemFontOptions() {
	const auto make = []() -> cairo_font_options_t* {
#ifdef LIB_UI_PANGO_OVER_FONTCONFIG
		const auto settings = Platform::FontSettings();
		if (!settings.antialias && !settings.hinting) {
			return nullptr;
		}
		const auto options = cairo_font_options_create();
		if (const auto antialias = settings.antialias) {
			cairo_font_options_set_antialias(options, [&] {
				switch (*antialias) {
				case Platform::FontAntialias::None:
					return CAIRO_ANTIALIAS_NONE;
				case Platform::FontAntialias::Subpixel:
					return CAIRO_ANTIALIAS_SUBPIXEL;
				}
				return CAIRO_ANTIALIAS_GRAY;
			}());

			// Taken only together with the antialiasing it belongs to: where
			// that one is grey the order means nothing, and the pattern is
			// asked for it anyway when the antialiasing is left unsaid. An
			// order that was not named is left at the default of cairo rather
			// than made up here, so that one coming from the surface still
			// gets through - merging copies over only what is not default.
			if (*antialias == Platform::FontAntialias::Subpixel) {
				if (const auto order = settings.subpixelOrder) {
					cairo_font_options_set_subpixel_order(options, [&] {
						switch (*order) {
						case Platform::FontSubpixelOrder::Bgr:
							return CAIRO_SUBPIXEL_ORDER_BGR;
						case Platform::FontSubpixelOrder::Vrgb:
							return CAIRO_SUBPIXEL_ORDER_VRGB;
						case Platform::FontSubpixelOrder::Vbgr:
							return CAIRO_SUBPIXEL_ORDER_VBGR;
						}
						return CAIRO_SUBPIXEL_ORDER_RGB;
					}());
				}
			}
		}
		if (const auto hinting = settings.hinting) {
			cairo_font_options_set_hint_style(options, [&] {
				switch (*hinting) {
				case Platform::FontHinting::None:
					return CAIRO_HINT_STYLE_NONE;
				case Platform::FontHinting::Slight:
					return CAIRO_HINT_STYLE_SLIGHT;
				case Platform::FontHinting::Medium:
					return CAIRO_HINT_STYLE_MEDIUM;
				}
				return CAIRO_HINT_STYLE_FULL;
			}());
		}
		return options;
#else // LIB_UI_PANGO_OVER_FONTCONFIG
		return nullptr;
#endif // !LIB_UI_PANGO_OVER_FONTCONFIG
	};
	return make();
}

// Put on the context, which copies them, so nothing of ours is kept: what the
// desktop says can be said again, and then this is how the new answer arrives.
void ApplySystemFontOptions(PangoContext *context) {
	const auto options = MakeSystemFontOptions();
	pango_cairo_context_set_font_options(context, options);
	if (options) {
		cairo_font_options_destroy(options);
	}
}

// Everything drawn from a font is drawn differently now, and none of it is
// text a widget knows it has to draw again - so they are told the way Qt tells
// them a font of the application changed, which ends in update() on every one
// of them and in the layouts around them being counted anew.
void NotifyFontOptionsChanged() {
	auto event = QEvent(QEvent::FontChange);
	for (const auto widget : QApplication::allWidgets()) {
		QCoreApplication::sendEvent(widget, &event);
	}
}

// Kept for the whole library: building it lists the fonts of the system once.
//
// A map of our own, not the default one of cairo: that one is shared with
// everything else in the process, and it lists the fonts the first time it is
// asked anything - which GTK, brought in by the platform theme of Qt, does
// while the application object is still being built, before the application
// has added the fonts that come with it. Nothing added afterwards is ever seen
// on that list. Ours is made no earlier than the first text is shaped, and by
// then the fonts of the application are registered, because they are what the
// styles are built from. It is still a cairo map, because cairo is what puts
// the pixels down: it rasterizes with the settings of the system, which is the
// whole point of not going through the font engine of Qt.
[[nodiscard]] PangoFontMap *FontMap() {
	static const auto result = pango_cairo_font_map_new();
	return result;
}

// Nothing is said here about rounding the positions of the glyphs to whole
// pixels: what a context says about it is read by the layout of Pango, which
// turns it into a flag of the shaping call - and the shaping here is done by
// hand, so the answer is given there instead.
//
// The settings of the desktop are put on the context, because that is what
// every font loaded through it is then made with - metrics and rasterization
// alike, instead of only the glyphs of a call that hands them over itself.
// Saying it again is all a change of them takes: Pango marks the context as
// changed and drops the fonts it made for the old answer.
[[nodiscard]] PangoContext *Context() {
	static const auto result = [] {
		const auto context = pango_font_map_create_context(FontMap());
		ApplySystemFontOptions(context);
#ifdef LIB_UI_PANGO_OVER_FONTCONFIG
		static auto lifetime = rpl::lifetime();
		Platform::FontSettingsChanges(
		) | rpl::on_next([=] {
			ApplySystemFontOptions(context);
			NotifyFontOptionsChanged();
		}, lifetime);
#endif // LIB_UI_PANGO_OVER_FONTCONFIG
		return context;
	}();
	return result;
}

// The pattern fontconfig matched for this font, which is what cairo rasterizes
// it by and what tells how it was hinted.
//
// Asking for it appeared in 1.48, and the field of the structure that held it
// before is deprecated now - so where the headers are too old the loader is
// asked for the newer call, which is there whenever the library that ends up
// loaded is newer, as it is wherever Pango comes from the system.
#ifdef LIB_UI_PANGO_OVER_FONTCONFIG
[[nodiscard]] FcPattern *FontPattern(PangoFont *font) {
	if (!PANGO_IS_FC_FONT(font)) {
		return nullptr;
	}
	const auto fc = PANGO_FC_FONT(font);
#if PANGO_VERSION_CHECK(1, 48, 0)
	return pango_fc_font_get_pattern(fc);
#elif __has_include(<dlfcn.h>) // Pango >= 1.48.0
	using Getter = FcPattern*(*)(PangoFcFont*);
	static const auto getter = reinterpret_cast<Getter>(
		dlsym(RTLD_DEFAULT, "pango_fc_font_get_pattern"));
	return getter ? getter(fc) : nullptr;
#else // Pango < 1.48.0 && __has_include(<dlfcn.h>)
	return nullptr;
#endif // Pango < 1.48.0 && !__has_include(<dlfcn.h>)
}
#endif // LIB_UI_PANGO_OVER_FONTCONFIG

// Whether the glyphs of this font may sit at a fraction of a pixel, and their
// advances keep one: hinting puts a stem on the grid, and text made of glyphs
// that were fitted to it is counted in whole pixels too. Qt keeps the fraction
// for light hinting and for none, and takes whole pixels otherwise - both in
// supportsHorizontalSubPixelPositions() and in shouldUseDesignMetrics(), of
// qfontengine_ft_p.h and qfontengine_ft.cpp - and the same is answered here,
// so that a hinted font is laid out the same by either backend.
//
// The question is about the font that will rasterize the glyphs, so the font
// itself is asked, and the two answers it has are put together the way cairo
// puts them - _cairo_ft_options_merge() in cairo-ft-font.c. One is what the
// font was loaded with, which is everything the context of ours was given; the
// other is the pattern fontconfig matched, read the way cairo reads it in
// _get_pattern_ft_options(). What the font was loaded with wins, except that a
// pattern with the hinting turned off leaves the glyphs unhinted whatever else
// says - the one rule of the merge that goes the other way. A pattern with no
// style in it at all is hinted in full, which is what Qt does with a pattern it
// can not read either.
[[nodiscard]] bool SupportsSubpixelPositions(PangoFont *font) {
#ifdef LIB_UI_PANGO_OVER_FONTCONFIG
	const auto pattern = FontPattern(font);
	auto hinting = FcTrue;
	if (pattern
		&& FcPatternGetBool(pattern, FC_HINTING, 0, &hinting) == FcResultMatch
		&& !hinting) {
		return true;
	}
	const auto scaled = PANGO_IS_CAIRO_FONT(font)
		? pango_cairo_font_get_scaled_font(PANGO_CAIRO_FONT(font))
		: nullptr;
	if (scaled && cairo_scaled_font_status(scaled) == CAIRO_STATUS_SUCCESS) {
		const auto options = cairo_font_options_create();
		const auto guard = gsl::finally([&] {
			cairo_font_options_destroy(options);
		});
		cairo_scaled_font_get_font_options(scaled, options);
		switch (cairo_font_options_get_hint_style(options)) {
		case CAIRO_HINT_STYLE_NONE:
		case CAIRO_HINT_STYLE_SLIGHT:
			return true;
		case CAIRO_HINT_STYLE_MEDIUM:
		case CAIRO_HINT_STYLE_FULL:
			return false;
		case CAIRO_HINT_STYLE_DEFAULT:
			break; // Nothing was said, so the pattern is what is left.
		}
	}
	if (!pattern) {
		return false;
	}
	auto style = FC_HINT_FULL;
	if (FcPatternGetInteger(pattern, FC_HINT_STYLE, 0, &style)
		!= FcResultMatch) {
		style = FC_HINT_FULL;
	}
	return (style == FC_HINT_NONE) || (style == FC_HINT_SLIGHT);
#else // LIB_UI_PANGO_OVER_FONTCONFIG
	return false;
#endif // !LIB_UI_PANGO_OVER_FONTCONFIG
}

// Shaped the way the layout of Pango shapes it: the geometry of the glyphs of
// a font that was fitted to whole pixels is put on whole pixels as well, and
// the glyphs of one that was not keep their fractions. Without this the glyphs
// of a hinted font come out on whole pixels anyway, because cairo rounds where
// it puts them, while the step to the next one keeps a fraction - and the gaps
// between the letters jump by a pixel.
//
// Saying so appeared in 1.44. That may be later than the headers this was
// built against while the library that ends up loaded is newer, as it is
// wherever Pango comes from the system - so when the headers are too old the
// loader is asked instead. Where even that is not available, an older Pango
// shapes the way it always did.
void Shape(
		const char *itemText,
		int itemLength,
		const char *paragraphText,
		int paragraphLength,
		const PangoAnalysis *analysis,
		PangoGlyphString *glyphs) {
	const auto rounds = !SupportsSubpixelPositions(analysis->font);
#if PANGO_VERSION_CHECK(1, 44, 0)
	pango_shape_with_flags(
		itemText,
		itemLength,
		paragraphText,
		paragraphLength,
		analysis,
		glyphs,
		rounds ? PANGO_SHAPE_ROUND_POSITIONS : PANGO_SHAPE_NONE);
#else // Pango >= 1.44.0
#if __has_include(<dlfcn.h>)
	using Shaper = void(*)(
		const char*,
		int,
		const char*,
		int,
		const PangoAnalysis*,
		PangoGlyphString*,
		int);
	static const auto shaper = reinterpret_cast<Shaper>(
		dlsym(RTLD_DEFAULT, "pango_shape_with_flags"));
	if (shaper) {
		shaper(
			itemText,
			itemLength,
			paragraphText,
			paragraphLength,
			analysis,
			glyphs,
			rounds ? 1 : 0); // PANGO_SHAPE_ROUND_POSITIONS, PANGO_SHAPE_NONE
		return;
	}
#endif // __has_include(<dlfcn.h>)
	pango_shape_full(
		itemText,
		itemLength,
		paragraphText,
		paragraphLength,
		analysis,
		glyphs);
#endif // Pango < 1.44.0
}

// The glyphs of a shaped item, drawn the way the font itself was loaded where
// that is what this drawing needs, and through a font asked for again where it
// is not - under a turn of the caller, or where subpixel antialiasing has to
// go. Pango is left to do it in the first case: it draws the same glyphs the
// same way, and it is the one that knows how to put a box with a code in it
// where a glyph is missing.
//
// The face comes from the font itself and is never asked for by name: a name
// goes through fontconfig again and can lead to another file - a family the
// configuration substitutes, a metric-compatible clone of it - and the glyphs
// of a shaped item are numbers that mean something else in another face.
void ShowGlyphs(
		cairo_t *context,
		PangoFont *font,
		PangoGlyphString *glyphs,
		bool subpixelAllowed) {
	// What the caller turns or scales the text by, which the glyphs have to be
	// rasterized through - the way the raster engine of Qt fills its cache of
	// them through the same matrix, instead of stretching what came out of it.
	auto turn = cairo_matrix_t();
	cairo_get_matrix(context, &turn);
	turn.x0 = turn.y0 = 0.;
	const auto turned = (turn.xx != 1.)
		|| (turn.yy != 1.)
		|| (turn.xy != 0.)
		|| (turn.yx != 0.);
	const auto scaled = ((!subpixelAllowed || turned)
		&& PANGO_IS_CAIRO_FONT(font))
		? pango_cairo_font_get_scaled_font(PANGO_CAIRO_FONT(font))
		: nullptr;
	if (!scaled) {
		pango_cairo_show_glyph_string(context, font, glyphs);
		return;
	}
	auto matrix = cairo_matrix_t();
	auto ctm = cairo_matrix_t();
	cairo_scaled_font_get_font_matrix(scaled, &matrix);
	cairo_scaled_font_get_ctm(scaled, &ctm);
	cairo_matrix_multiply(&ctm, &ctm, &turn);

	// Where the font of the item is taken as it is, it still has to be asked
	// for again with the matrix above - and a font asked for by its face keeps
	// only what its pattern said, so what the context gave this one is taken
	// off the font itself and handed over again. Everything the desktop said
	// is in there, because that is what the context was built with.
	const auto options = cairo_font_options_create();
	const auto guardOptions = gsl::finally([&] {
		cairo_font_options_destroy(options);
	});
	cairo_scaled_font_get_font_options(scaled, options);

	// Glyphs that go into an image of ours can not keep subpixel antialiasing:
	// what lies under them there is not the screen - see the notes there.
	if (!subpixelAllowed) {
		cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
		cairo_font_options_set_subpixel_order(
			options,
			CAIRO_SUBPIXEL_ORDER_DEFAULT);
	}
	const auto with = cairo_scaled_font_create(
		cairo_scaled_font_get_font_face(scaled),
		&matrix,
		&ctm,
		options);
	const auto guard = gsl::finally([&] {
		cairo_scaled_font_destroy(with);
	});
	if (cairo_scaled_font_status(with) != CAIRO_STATUS_SUCCESS) {
		pango_cairo_show_glyph_string(context, font, glyphs);
		return;
	}

	// Where the glyphs go, counted the way Pango counts it: from the point the
	// caller moved to, each glyph at an offset of its own, and the next one an
	// advance further along.
	struct Missing {
		PangoGlyphInfo glyph = {};
		double x = 0.;
		double y = 0.;
	};
	auto x = 0.;
	auto y = 0.;
	cairo_get_current_point(context, &x, &y);
	auto list = QVarLengthArray<cairo_glyph_t, 64>();
	auto missing = QVarLengthArray<Missing, 4>();
	for (auto i = 0; i != glyphs->num_glyphs; ++i) {
		const auto &glyph = glyphs->glyphs[i];
		const auto atX = x + (glyph.geometry.x_offset / double(PANGO_SCALE));
		const auto atY = y + (glyph.geometry.y_offset / double(PANGO_SCALE));
		if (glyph.glyph & PANGO_GLYPH_UNKNOWN_FLAG) {
			// The box with the code of the character in it, which only Pango
			// knows how to draw - and it is what the text gets where this is
			// drawn without options of ours, so it is what it gets here too.
			missing.push_back({ glyph, atX, atY });
		} else if (glyph.glyph != PANGO_GLYPH_EMPTY) {
			list.push_back({
				.index = glyph.glyph,
				.x = atX,
				.y = atY,
			});
		}
		x += glyph.geometry.width / double(PANGO_SCALE);
	}
	cairo_set_scaled_font(context, with);
	cairo_show_glyphs(context, list.data(), list.size());

	for (auto &one : missing) {
		auto cluster = 0;
		auto glyph = one.glyph;
		glyph.geometry.x_offset = 0;
		glyph.geometry.y_offset = 0;
		auto single = PangoGlyphString{
			.num_glyphs = 1,
			.glyphs = &glyph,
			.log_clusters = &cluster,
		};
		cairo_move_to(context, one.x, one.y);
		pango_cairo_show_glyph_string(context, font, &single);
	}
}

// Straight into the buffer the painter draws to, so that cairo blends the
// glyphs against the real background and the subpixel antialiasing of the
// system survives. Answers whether it could.
[[nodiscard]] bool drawInPlace(
		QPainter &p,
		QPointF at,
		PangoFont *font,
		PangoGlyphString *glyphs) {
	// Painting a widget, the device of the painter is the widget itself and
	// the pixels live in the buffer of the window behind it - which the engine
	// is given and the painter is not.
	const auto device = p.paintEngine()->paintDevice();
	if (device->devType() != QInternal::Image) {
		return false;
	}

	const auto image = static_cast<QImage*>(device);
	const auto format = image->format();
	if (format != QImage::Format_ARGB32_Premultiplied
		&& format != QImage::Format_RGB32) {
		return false;
	} else if (p.compositionMode() != QPainter::CompositionMode_SourceOver) {
		return false;
	}

	// Of the device, not of the world: a widget is painted into a shared
	// buffer at its own place in it, and that offset lives only here.
	//
	// A projection is where the raster engine of Qt gives up on its glyphs as
	// well - shouldDrawCachedGlyphs() in qpaintengine_raster.cpp - and draws
	// outlines instead, which is what the image below comes closest to.
	const auto transform = p.deviceTransform();
	const auto ratio = image->devicePixelRatio();
	if (transform.type() >= QTransform::TxProject) {
		return false;
	}

	const auto surface = cairo_image_surface_create_for_data(
		image->bits(),
		(format == QImage::Format_RGB32) ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32,
		image->width(),
		image->height(),
		int(image->bytesPerLine()));
	const auto context = cairo_create(surface);
	const auto guard = gsl::finally([&] {
		cairo_destroy(context);
		cairo_surface_destroy(surface);
	});

	// Two clips have to be kept, and only one of them is the caller's:
	// hasClipping() answers about the clip that was set on the painter, while
	// the region a widget is being repainted in comes from the system and is
	// invisible there. Painting outside it would put the glyphs over what is
	// already on the screen.
	auto clip = QRegion();
	auto clipping = false;
	const auto system = p.paintEngine()->systemClip();
	if (!system.isEmpty()) {
		// Already in the pixels of the device.
		clip = system;
		clipping = true;
	}
	if (p.hasClipping()) {
		const auto user = transform.map(p.clipRegion());
		clip = clipping ? (clip & user) : user;
		clipping = true;
	}
	if (clipping) {
		if (clip.isEmpty()) {
			return true;
		}
		for (const auto &rect : clip) {
			cairo_rectangle(
				context,
				rect.x(),
				rect.y(),
				rect.width(),
				rect.height());
		}
		cairo_clip(context);
	}

	// The scale by the ratio of the device is in the glyphs already, because
	// that is what they were shaped in - so what is left of the transform of
	// the painter goes on the context, and cairo puts the glyphs through it.
	// The clip above was set before it, so it stays in the pixels it came in.
	const auto rest = QTransform::fromScale(1. / ratio, 1. / ratio) * transform;
	const auto turn = cairo_matrix_t{
		.xx = rest.m11(),
		.yx = rest.m12(),
		.xy = rest.m21(),
		.yy = rest.m22(),
		.x0 = rest.dx(),
		.y0 = rest.dy(),
	};
	cairo_set_matrix(context, &turn);
	const auto position = at * ratio;

	// Coverage per colour channel comes back as a tint from a pixel that is
	// composited again, so the glyphs are asked for grey everywhere but the
	// buffer of the window. Being painted as a widget does not say which of
	// the two it is - the device of the painter stays the widget either way -
	// and the buffer is asked only of a window that was exposed: the paint
	// device of a backing store is valid while it is being painted, and one
	// that never painted has no buffer to point into. What this does not
	// catch is text over a part of the window that is still transparent.
	const auto widget = (p.device()->devType() == QInternal::Widget)
		? static_cast<QWidget*>(p.device())
		: nullptr;
	const auto window = widget ? widget->window()->windowHandle() : nullptr;
	const auto store = (window && window->isExposed())
		? widget->window()->backingStore()
		: nullptr;
	const auto onScreen = store && (store->paintDevice() == device);
	const auto color = p.pen().color();
	cairo_set_source_rgba(
		context,
		color.redF(),
		color.greenF(),
		color.blueF(),
		color.alphaF() * p.opacity());

	cairo_move_to(context, position.x(), position.y());
	ShowGlyphs(context, font, glyphs, onScreen);

	return true;
}

} // namespace

struct Paragraph::State {
	Text text;
	int position = 0;

	// Itemized once for the whole paragraph, because that is the unit a
	// direction is resolved over: itemizing a line on its own would put a
	// trailing bracket or digit on the wrong side. The lines cut what they
	// need out of this.
	GList *items = nullptr;

	// The same items, so that a line finds the ones it covers by halving
	// instead of walking over every item of the paragraph - which is what it
	// would cost every line of it, and once more for every question asked
	// about a point in it.
	std::vector<PangoItem*> list;

	qreal ratio = 1.;

	// In characters, of the text that was itemized - which is not the same as
	// the levels below once an ellipsis was added past its end.
	int length = 0;

	// Where a line put an ellipsis, in characters of the paragraph. Beyond it
	// the text of a line is its own and no longer the paragraph's, however
	// short that line is.
	int elideAt = -1;

	// One per character, for the ellipsis a line may add past the end of the
	// paragraph, which was never itemized with it.
	std::vector<uchar> levels;

	~State() {
		if (items) {
			g_list_free_full(items, [](gpointer item) {
				pango_item_free(static_cast<PangoItem*>(item));
			});
		}
	}
};

Paragraph::Paragraph() = default;

Paragraph::Paragraph(Paragraph &&other) = default;

Paragraph &Paragraph::operator=(Paragraph &&other) = default;

Paragraph::~Paragraph() = default;

void Paragraph::clear() {
	_state = nullptr;
}

bool Paragraph::ready() const {
	return _state != nullptr;
}

namespace {

// What a resolved paragraph was made from. A string that was given other text
// keeps its address, so where its text lives and how long it is are a part of
// this too.
struct ResolvedKey {
	const void *owner = nullptr;
	uint version = 0;
	int position = 0;
	int length = 0;

	// In 1/65536 of it: a key is compared for equality, a real is not.
	int ratio = 0;

	int blockIndexHint = 0;
	int blockIndexLimit = 0;
	bool baseRtl = false;

	friend bool operator==(const ResolvedKey &, const ResolvedKey &)
		= default;
};

[[nodiscard]] int RatioKey(qreal ratio) {
	return qRound(ratio * 65536.);
}

// A few of them, because a question about a point walks the paragraphs of the
// text one after another, and one kept answer would be thrown away by the next
// paragraph before it was ever asked for again.
constexpr auto kKeptParagraphs = 4;

// Kept without a type of its own: what a paragraph is made of belongs to it
// and is not named outside of it.
struct Resolved {
	ResolvedKey key;
	std::shared_ptr<void> state;
};
std::array<Resolved, kKeptParagraphs> Kept;
int KeptNext/* = 0*/;

[[nodiscard]] std::shared_ptr<void> KeptFor(const ResolvedKey &key) {
	for (const auto &entry : Kept) {
		if (entry.state && entry.key == key) {
			return entry.state;
		}
	}
	return nullptr;
}

void Keep(const ResolvedKey &key, std::shared_ptr<void> state) {
	Kept[KeptNext] = { key, std::move(state) };
	KeptNext = (KeptNext + 1) % kKeptParagraphs;
}

void Forget(const void *state) {
	for (auto &entry : Kept) {
		if (entry.state.get() == state) {
			entry = {};
		}
	}
}

} // namespace

void Paragraph::resolve(
		not_null<const String*> t,
		int position,
		int length,
		bool baseRtl,
		int blockIndexHint,
		int blockIndexLimit,
		qreal ratio) {
	if (ready()) {
		return;
	}

	// The engine resolves a paragraph for every question it is asked about a
	// point, and there is one question per pixel of a hit test - while the
	// answer depends on nothing that changes in between. So the last one is
	// kept and handed out again: what it was made from is remembered with it,
	// down to where the text of the string lives, because a string that was
	// given other text is another paragraph.
	// The address of a string is not enough to tell one text from another: a
	// string keeps it while the text in it is replaced, and a string that is
	// gone leaves it to the next one. What it counts its own changes with is.
	const auto key = ResolvedKey{
		.owner = t.get(),
		.version = t->_version,
		.position = position,
		.length = length,
		.ratio = RatioKey(ratio),
		.blockIndexHint = blockIndexHint,
		.blockIndexLimit = blockIndexLimit,
		.baseRtl = baseRtl,
	};
	if (auto kept = KeptFor(key)) {
		_state = std::static_pointer_cast<State>(std::move(kept));
		return;
	}
	_state = std::make_shared<State>(State{
		.text = Text(QStringView(t->_text).mid(position, length)),
		.position = position,
		.ratio = ratio,
		.length = length,
	});
	if (!length) {
		Keep(key, _state);
		return;
	}
	_state->levels.resize(length, uchar(baseRtl ? 1 : 0));

	const auto attributes = pango_attr_list_new();

	const auto blocks = &t->_blocks;
	const auto limit = (blockIndexLimit >= 0)
		? blockIndexLimit
		: int(blocks->size());
	for (auto i = blockIndexHint; i != limit; ++i) {
		const auto block = (*blocks)[i].get();
		const auto from = std::max(int(block->position()) - position, 0);
		const auto till = std::min(
			((i + 1 < int(blocks->size()))
				? int((*blocks)[i + 1]->position())
				: (position + length)) - position,
			length);
		if (till <= from) {
			continue;
		}
		const auto start = _state->text.toUtf8(from);
		const auto end = _state->text.toUtf8(till);
		const auto font = WithFlags(t->_st->font, block->flags());

		// An object block is not text at all: it takes the width its own
		// painting needs, nothing is shaped for it, and the only thing Pango
		// has to know about it is that it stands apart. A font of it would
		// only cost the itemizer more work to reach the same place.
		const auto type = block->type();
		const auto object = (type == TextBlockType::Emoji)
			|| (type == TextBlockType::CustomEmoji)
			|| (type == TextBlockType::Skip);
		if (object) {
			auto rect = PangoRectangle{
				.width = ToPango(Fixed(block->objectWidth()), ratio),
				.height = ToPango(Fixed(font->height), ratio),
			};
			rect.y = -ToPango(Fixed(font->ascent), ratio);
			const auto shape = pango_attr_shape_new(&rect, &rect);
			shape->start_index = start;
			shape->end_index = end;
			pango_attr_list_insert(attributes, shape);

			// Nothing is shaped for an object, so it needs no font - and the
			// range of this attribute already keeps the itemizer from letting
			// an item cross into the block, which is what the rest of them
			// would have been for.
			continue;
		}

		// One attribute per block and not one per stretch of them: an item
		// takes the block of the position it starts at, and decides by it
		// whether it is an object at all - so an item must never cross from
		// one block into another, and an attribute of every block is what
		// keeps the itemizer from letting it.
		const auto add = [&](PangoAttribute *attribute) {
			attribute->start_index = start;
			attribute->end_index = end;
			pango_attr_list_insert(attributes, attribute);
		};
		const auto description = FontDescription(font->f, ratio);
		add(pango_attr_font_desc_new(description));
		pango_font_description_free(description);
	}

	_state->items = pango_itemize_with_base_dir(
		Context(),
		baseRtl ? PANGO_DIRECTION_RTL : PANGO_DIRECTION_LTR,
		_state->text.data(),
		0,
		_state->text.size(),
		attributes,
		nullptr); // cached_iter
	pango_attr_list_unref(attributes);

	for (auto i = _state->items; i != nullptr; i = i->next) {
		const auto item = static_cast<PangoItem*>(i->data);
		_state->list.push_back(item);
		const auto from = _state->text.toUtf16(item->offset);
		const auto till = _state->text.toUtf16(item->offset + item->length);
		for (auto j = from; j != till; ++j) {
			_state->levels[j] = uchar(item->analysis.level);
		}
	}

	Keep(key, _state);
}

void Paragraph::elide(int position, int count, bool baseRtl) {
	if (!_state) {
		return;
	}

	// The ellipsis goes into the paragraph itself, so what is kept aside must
	// not be handed to anyone expecting a paragraph without one.
	Forget(_state.get());

	_state->elideAt = position;
	const auto length = position + count;
	if (length > int(_state->levels.size())) {
		_state->levels.resize(length, uchar(baseRtl ? 1 : 0));
	}
	// The ellipsis goes the way the text before it goes.
	const auto level = (length > count)
		? _state->levels[length - count - 1]
		: uchar(baseRtl ? 1 : 0);
	for (auto i = count; i > 0; --i) {
		_state->levels[length - i] = level;
	}
}

// Line breaks and caret positions of one line, which Pango works out in a
// pass over the whole text that costs as much as the shaping does. Drawing a
// line never asks about them - only a click and an elision in the middle do -
// so they are left until something does.
class LineAttributes final {
public:
	void set(not_null<const Text*> text) {
		_text = text;
	}
	[[nodiscard]] gsl::span<const CharAttribute> resolve();

private:
	const Text *_text = nullptr;
	std::vector<CharAttribute> _list;
	bool _resolved = false;

};

gsl::span<const CharAttribute> LineAttributes::resolve() {
	if (_resolved) {
		return _list;
	}
	_resolved = true;

	// The three answers Pango works out in pango_get_log_attrs(), taken from
	// Qt instead: measured on the same texts, the pass of Pango costs four to
	// five times what this one does, and the answers come out at the
	// characters the engine counts in - so nothing has to be spread from the
	// code points Pango answers about.
	//
	// The answers are taken as Qt gives them, and not bent to what Pango
	// answered: the engine this is a port of asks the same
	// QUnicodeTools::initCharAttributes through QTextEngine, and its white
	// space is QChar::isSpace() - so following Pango here would be following
	// the wrong one of the two.
	const auto &text = _text->utf16();
	const auto length = int(text.size());
	_list.assign(length + 1, CharAttribute());

	auto grapheme = QTextBoundaryFinder(QTextBoundaryFinder::Grapheme, text);
	do {
		_list[grapheme.position()].graphemeBoundary = true;
	} while (grapheme.toNextBoundary() >= 0);

	auto line = QTextBoundaryFinder(QTextBoundaryFinder::Line, text);
	do {
		_list[line.position()].lineBreak = true;
	} while (line.toNextBoundary() >= 0);

	for (auto i = 0; i != length; ++i) {
		_list[i].whiteSpace = text.at(i).isSpace();
	}
	return _list;
}

// Where the letters of a line begin among the glyphs they are drawn as, for
// every item of it in one place: Pango answers where a cluster is drawn with a
// walk over every glyph of the item, and the layout asks it about every
// cluster - while a table of its own for every item would be an allocation for
// every item, and a line of emoji holds a thousand of them.
struct ClusterTables {
	// In characters of the item, and in the order they are written in.
	std::vector<int> starts;

	// How far the left edge of each is from the left edge of the item, in the
	// units of Pango, and the first glyph it is drawn as.
	std::vector<int> xs;
	std::vector<int> glyphs;
};

// Everything a shaped item is, kept where only this backend can see it.
struct ShapeEntry {
	PangoItem *item = nullptr;
	PangoGlyphString *glyphs = nullptr;

	// The line text, so that character offsets can be turned into the byte
	// offsets Pango works in.
	const Text *text = nullptr;

	// The attributes of the whole line and where this item starts in it.
	LineAttributes *attributes = nullptr;
	int attributesShift = 0;

	// The clusters of the line and the range in them that is this item's, and
	// where the last question about them was answered: they are asked about in
	// the order the letters go far more often than not, so the next answer is
	// almost always the next entry, and a search over the whole table for each
	// of them is work that the walk itself already did.
	const ClusterTables *tables = nullptr;
	int clusterFrom = 0;
	int clusterCount = 0;
	mutable int clusterHint = 0;

	// Where the end of the item is, which is its width for text that goes to
	// the right and the left edge for text that goes to the left.
	int endX = 0;

	int position = 0;
	int length = 0;
	qreal ratio = 1.;
};

// Shifted so that index zero is the first character of the item.
[[nodiscard]] const CharAttribute *Attributes(const ShapeEntry &entry) {
	return entry.attributes->resolve().data() + entry.attributesShift;
}

struct LineShaper::Backend {
	Backend(
		not_null<const String*> t,
		Paragraph &paragraph,
		int offset,
		const QString &text,
		int blockIndexHint,
		int blockIndexLimit);
	~Backend();

	// Cut out of the paragraph, owned here. An ellipsis a line ends with is
	// not a part of the paragraph and is itemized from the line's own text,
	// which is what the flag is for: the two count bytes from different ends.
	struct Piece {
		PangoItem *item = nullptr;
		bool own = false;
	};
	std::vector<Piece> items;

	// An item is described in Pango by byte offsets into the paragraph, and
	// asked about by the callers in characters of the line.
	[[nodiscard]] int itemPosition(const Piece &piece) const;
	[[nodiscard]] int itemLength(const Piece &piece) const;
	[[nodiscard]] const Text &pieceText(const Piece &piece) const;
	[[nodiscard]] int blockIndexAt(int position) const;

	const not_null<const String*> t;
	Paragraph::State &paragraph;

	// The line's own text, which the paragraph does not hold when an ellipsis
	// was added to it.
	Text text;
	int offset = 0;


	int first = 0;

	// Every item of the line, and the ones the caller asked to shape, which
	// is a range out of them.
	std::vector<Item> list;
	std::vector<Item> drawn;
	std::vector<PangoGlyphString*> strings;
	std::vector<int> visualOrder;
	LineAttributes attributes;
	ClusterTables tables;
	std::vector<ShapeEntry> entries;
	std::vector<ShapedItem> shaped;
};

LineShaper::Backend::Backend(
	not_null<const String*> t,
	Paragraph &paragraph,
	int offset,
	const QString &text,
	int blockIndexHint,
	int blockIndexLimit)
: t(t)
, paragraph(*paragraph._state)
, text(text)
, offset(offset) {
	const auto &state = this->paragraph;
	const auto from = offset - state.position;
	const auto till = from + int(text.size());

	// The paragraph was itemized without the ellipsis a line may end with, so
	// its part of the line is itemized on its own and put at the end.
	const auto inParagraph = std::min(
		till,
		(state.elideAt >= 0) ? state.elideAt : state.length);
	// The items of a paragraph follow each other, so the ones of a line are a
	// range out of them: the first is the one the line starts in, and the walk
	// stops as soon as an item begins past where the line ends.
	const auto begins = ranges::upper_bound(
		state.list,
		from,
		ranges::less(),
		[&](PangoItem *item) {
			return state.text.toUtf16(item->offset + item->length);
		});
	const auto ends = end(state.list);
	for (auto i = begins; i != ends; ++i) {
		const auto item = *i;
		const auto start = state.text.toUtf16(item->offset);
		if (start >= inParagraph) {
			break;
		}
		const auto finish = state.text.toUtf16(item->offset + item->length);
		// Pango counts characters in code points, where a pair of surrogates
		// is one and the engine counts two, so every cut is measured in bytes
		// and the count that goes with it is taken from the text itself.
		const auto copy = pango_item_copy(item);
		if (start < from) {
			// Everything before the line belongs to the line before it.
			const auto cut = state.text.toUtf8(from) - copy->offset;
			if (cut > 0 && cut < copy->length) {
				const auto points = g_utf8_strlen(
					state.text.data() + copy->offset,
					cut);
				pango_item_free(pango_item_split(copy, cut, points));
			}
		}
		if (finish > inParagraph) {
			const auto keep = state.text.toUtf8(inParagraph) - copy->offset;
			if (keep > 0 && keep < copy->length) {
				const auto points = g_utf8_strlen(
					state.text.data() + copy->offset,
					keep);
				const auto head = pango_item_split(copy, keep, points);
				pango_item_free(copy);
				items.push_back({ .item = head });
				continue;
			}
		}
		items.push_back({ .item = copy });
	}

	// What the line added past the end of the paragraph, which is an ellipsis
	// and goes the way the text before it goes.
	if (till > inParagraph) {
		const auto start = this->text.toUtf8(inParagraph - from);
		const auto level = state.levels.empty()
			? uchar(0)
			: state.levels[std::min(
				int(state.levels.size()) - 1,
				std::max(inParagraph - 1, 0))];
		// With the font of the text it follows: without one Pango picks
		// whatever the system defaults to, and the ellipsis comes out in a
		// different face than the line it ends.
		const auto attributes = pango_attr_list_new();
		const auto description = FontDescription(
			t->_st->font->f,
			state.ratio);
		const auto attribute = pango_attr_font_desc_new(description);
		attribute->start_index = start;
		attribute->end_index = this->text.size();
		pango_attr_list_insert(attributes, attribute);
		pango_font_description_free(description);

		const auto tail = pango_itemize_with_base_dir(
			Context(),
			(level % 2) ? PANGO_DIRECTION_RTL : PANGO_DIRECTION_LTR,
			this->text.data(),
			start,
			this->text.size() - start,
			attributes,
			nullptr); // cached_iter
		pango_attr_list_unref(attributes);
		for (auto i = tail; i != nullptr; i = i->next) {
			items.push_back({
				.item = static_cast<PangoItem*>(i->data),
				.own = true,
			});
		}
		g_list_free(tail);
	}

	attributes.set(&this->text);

	// Neutral descriptions of the items, with the geometry left until they
	// are shaped.
	list.reserve(items.size());
	for (const auto &piece : items) {
		const auto item = piece.item;
		const auto position = itemPosition(piece);
		const auto length = itemLength(piece);
		const auto block = blockIndexAt(position);
		const auto type = t->_blocks[block]->type();
		// An emoji block ends with the spaces that followed it in the text,
		// and those are text: the object is only what the block paints, and
		// counting them in would paint it once more. The ellipsis a line adds
		// belongs to no block at all.
		const auto space = (position >= 0)
			&& (position < int(text.size()))
			&& text.at(position).isSpace();
		const auto object = !piece.own
			&& !space
			&& (type == TextBlockType::Emoji
				|| type == TextBlockType::CustomEmoji
				|| type == TextBlockType::Skip);
		list.push_back(Item{
			.position = position,
			.length = length,
			.blockIndex = block,
			.bidiLevel = uchar(item->analysis.level),
			.object = object,
			.newline = !piece.own && (type == TextBlockType::Newline),
		});
	}
}

const Text &LineShaper::Backend::pieceText(const Piece &piece) const {
	return piece.own ? text : paragraph.text;
}

int LineShaper::Backend::itemPosition(const Piece &piece) const {
	const auto &from = pieceText(piece);
	const auto at = from.toUtf16(piece.item->offset);
	return piece.own ? at : (at + paragraph.position - offset);
}

int LineShaper::Backend::itemLength(const Piece &piece) const {
	const auto &from = pieceText(piece);
	return from.toUtf16(piece.item->offset + piece.item->length)
		- from.toUtf16(piece.item->offset);
}

int LineShaper::Backend::blockIndexAt(int position) const {
	const auto at = offset + position;
	const auto &blocks = t->_blocks;
	auto i = ranges::upper_bound(
		blocks,
		at,
		ranges::less(),
		[](const Block &block) { return int(block->position()); });
	return int((i == begin(blocks)) ? 0 : ((i - 1) - begin(blocks)));
}

LineShaper::Backend::~Backend() {
	for (const auto &piece : items) {
		pango_item_free(piece.item);
	}
	for (const auto glyphs : strings) {
		if (glyphs) {
			pango_glyph_string_free(glyphs);
		}
	}
}

LineShaper::LineShaper(
	not_null<const String*> t,
	Paragraph &paragraph,
	int offset,
	const QString &text,
	int blockIndexHint,
	int blockIndexLimit)
: _backend(std::make_unique<Backend>(
	t,
	paragraph,
	offset,
	text,
	blockIndexHint,
	blockIndexLimit)) {
}

LineShaper::~LineShaper() = default;

int LineShaper::findItem(int position) const {
	const auto &list = _backend->list;
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		if (position < list[i].position + list[i].length) {
			return (position < list[i].position) ? -1 : i;
		}
	}
	return -1;
}

const std::vector<Item> &LineShaper::items() const {
	return _backend->drawn;
}

gsl::span<const CharAttribute> LineShaper::attributes() const {
	return _backend->attributes.resolve();
}

gsl::span<const int> LineShaper::visualOrder() const {
	return _backend->visualOrder;
}

int LineShaper::blockIndex(int position) const {
	return _backend->blockIndexAt(position);
}

int LineShaper::itemLength(int index) const {
	return _backend->drawn[index].length;
}

void LineShaper::shapeRange(int firstItem, int lastItem) {
	auto &backend = *_backend;
	const auto count = lastItem - firstItem + 1;
	backend.first = firstItem;
	backend.drawn.assign(
		begin(backend.list) + firstItem,
		begin(backend.list) + lastItem + 1);
	backend.entries.assign(count, ShapeEntry());
	backend.shaped.assign(count, ShapedItem());
	backend.strings.resize(count);
	backend.tables.starts.clear();
	backend.tables.xs.clear();
	backend.tables.glyphs.clear();

	auto skipIndex = -1;
	for (auto i = 0; i != count; ++i) {
		const auto &piece = backend.items[firstItem + i];
		const auto item = piece.item;
		auto &drawn = backend.drawn[i];
		if (backend.t->_blocks[drawn.blockIndex]->type()
			== TextBlockType::Skip) {
			// A skip is laid out as if it had no direction of its own, so
			// that it stays at the end of the line it trails.
			item->analysis.level = 0;
			drawn.bidiLevel = 0;
			skipIndex = i;
		}
		const auto glyphs = pango_glyph_string_new();
		backend.strings[i] = glyphs;
		if (drawn.object) {
			// An object is not text: it takes the width its own painting
			// needs and nothing is shaped for it. Pango would only know that
			// from a shape attribute, which its layout honours and the
			// shaping call used here does not - so it is answered here, and
			// the glyphs of it stay empty.
			const auto block = backend.t->_blocks[drawn.blockIndex].get();
			drawn.width = Fixed(block->objectWidth());
			continue;
		}
		const auto &text = backend.pieceText(piece);
		Shape(
			text.data() + item->offset,
			item->length,
			text.data(),
			text.size(),
			&item->analysis,
			glyphs);

		auto ink = PangoRectangle();
		auto logical = PangoRectangle();
		pango_glyph_string_extents(glyphs, item->analysis.font, &ink, &logical);
		const auto ratio = backend.paragraph.ratio;
		drawn.width = FromPango(logical.width, ratio);
		drawn.ascent = FromPango(-logical.y, ratio).toInt();
		drawn.descent = FromPango(logical.height + logical.y, ratio).toInt();
	}

	backend.visualOrder.resize(count);
	ReorderVisually(backend.drawn, backend.visualOrder);
	if (style::RightToLeft() && skipIndex == count - 1) {
		for (auto i = count; i > 1;) {
			--i;
			backend.visualOrder[i] = backend.visualOrder[i - 1];
		}
		backend.visualOrder[0] = skipIndex;
	}
}

const ShapedItem &LineShaper::shape(int index) {
	auto &backend = *_backend;
	auto &shaped = backend.shaped[index];
	if (shaped) {
		return shaped;
	}
	const auto &piece = backend.items[backend.first + index];
	const auto item = piece.item;
	const auto &drawn = backend.drawn[index];
	auto &entry = backend.entries[index];
	entry.item = item;
	entry.glyphs = backend.strings[index];
	entry.text = &backend.pieceText(piece);
	entry.attributes = &backend.attributes;
	entry.attributesShift = drawn.position;
	entry.position = entry.text->toUtf16(item->offset);
	entry.length = drawn.length;
	entry.ratio = backend.paragraph.ratio;

	// The same walk Pango takes to answer where a cluster is drawn, taken
	// once for all of them: the clusters come in the order they are written
	// in when the glyphs are walked the way the item goes, and the distance
	// to each of them is what was added up before it.
	auto &tables = backend.tables;
	entry.tables = &tables;
	entry.clusterFrom = int(tables.starts.size());
	const auto glyphs = entry.glyphs;
	const auto rtl = (drawn.bidiLevel % 2) != 0;
	const auto base = entry.position;
	auto x = 0;
	auto cluster = -1;
	const auto add = [&](int index) {
		if (glyphs->log_clusters[index] != cluster) {
			cluster = glyphs->log_clusters[index];
			const auto at = entry.text->toUtf16(item->offset + cluster) - base;
			if (int(tables.starts.size()) == entry.clusterFrom
				|| tables.starts.back() != at) {
				tables.starts.push_back(at);
				tables.xs.push_back(x);
				tables.glyphs.push_back(index);
				return;
			}
		}
		// The glyphs of a letter are walked the other way around when the text
		// goes to the left, so the first of them is met last.
		tables.glyphs.back() = std::min(tables.glyphs.back(), index);
	};
	if (rtl) {
		// Right to left, where the glyphs are drawn the other way around, so
		// the first letter is the last glyph and sits at the right edge.
		for (auto i = 0; i != glyphs->num_glyphs; ++i) {
			x += glyphs->glyphs[i].geometry.width;
		}
		for (auto i = glyphs->num_glyphs; i != 0;) {
			add(--i);
			x -= glyphs->glyphs[i].geometry.width;
		}
		entry.endX = 0;
	} else {
		for (auto i = 0; i != glyphs->num_glyphs; ++i) {
			add(i);
			x += glyphs->glyphs[i].geometry.width;
		}
		entry.endX = x;
	}
	entry.clusterCount = int(tables.starts.size()) - entry.clusterFrom;

	shaped._entry = &entry;
	shaped._length = drawn.length;
	shaped._rtl = (drawn.bidiLevel % 2) != 0;
	return shaped;
}


namespace {

// Where a letter starts among the clusters the item was shaped into, in the
// table of the line, or -1 when it starts inside one of them - a letter drawn
// as a part of the glyph of the one before it has no place of its own there.
[[nodiscard]] int ClusterAt(const ShapeEntry &entry, int offset) {
	// Both halves of a surrogate pair are one letter and point at the byte the
	// pair begins with, so a question about the second half is one about the
	// first.
	const auto byte = entry.text->toUtf8(entry.position + offset);
	const auto at = entry.text->toUtf16(byte) - entry.position;
	const auto begins = begin(entry.tables->starts);
	const auto from = begins + entry.clusterFrom;
	const auto till = from + entry.clusterCount;

	// From where the one before was answered, when the question is not behind
	// it - the table is sorted, so the answer can only be there or later.
	const auto hinted = from + std::clamp(
		entry.clusterHint,
		0,
		entry.clusterCount);
	const auto i = ((hinted != till) && (*hinted <= at))
		? std::lower_bound(hinted, till, at)
		: std::lower_bound(from, till, at);
	entry.clusterHint = int(i - from);
	return (i != till && *i == at) ? int(i - begins) : -1;
}

// The glyph a letter is drawn as, or -1 when it is drawn as a part of another.
[[nodiscard]] int GlyphAt(const ShapeEntry &entry, int offset) {
	const auto cluster = ClusterAt(entry, offset);
	return (cluster < 0) ? -1 : entry.tables->glyphs[cluster];
}

// Distance from the left edge of the item to a character, which is what Pango
// answers even for a right-to-left item, where the glyphs run the other way.
// In the units of Pango, which are finer than the ones of the engine: a
// distance is the difference of two of these, and turning each of them into
// the coarser units first would round twice for one answer.
[[nodiscard]] int XAt(const ShapeEntry &entry, int offset) {
	if (!entry.clusterCount) {
		// Nothing was drawn for the item, and every position in it is the
		// same one.
		return 0;
	} else if (offset >= entry.length) {
		return entry.endX;
	}
	const auto cluster = ClusterAt(entry, offset);
	if (cluster >= 0) {
		return entry.tables->xs[cluster];
	}

	// Inside a letter that is drawn as one glyph, where the answer is not an
	// edge of anything: the font may say where a caret goes inside a ligature,
	// and only Pango knows how to ask it.
	const auto byte = entry.text->toUtf8(entry.position + offset)
		- entry.text->toUtf8(entry.position);
	auto result = 0;
	pango_glyph_string_index_to_x(
		entry.glyphs,
		const_cast<char*>(entry.text->data() + entry.item->offset),
		entry.item->length,
		&entry.item->analysis,
		byte,
		FALSE, // trailing
		&result);
	return result;
}

} // namespace

Fixed ShapedItem::width() const {
	return width(0, _length);
}

Fixed ShapedItem::width(int fromOffset, int tillOffset) const {
	if (fromOffset >= tillOffset) {
		// An empty range of characters is an empty range of glyphs, and not
		// the rest of the item - which is what the end of it maps to.
		return {};
	}
	const auto &entry = *_entry;
	const auto from = XAt(entry, fromOffset);
	const auto till = XAt(entry, tillOffset);
	// In a right-to-left item the later character sits to the left.
	return FromPango(_rtl ? (from - till) : (till - from), entry.ratio);
}

int ShapedItem::clusterEnd(int offset, int tillOffset) const {
	const auto &entry = *_entry;
	const auto from = begin(entry.tables->starts) + entry.clusterFrom;
	const auto till = from + entry.clusterCount;
	const auto hinted = from + std::clamp(
		entry.clusterHint,
		0,
		entry.clusterCount);
	const auto i = ((hinted != till) && (*hinted <= offset))
		? std::upper_bound(hinted, till, offset)
		: std::upper_bound(from, till, offset);
	return (i != till) ? std::min(*i, tillOffset) : tillOffset;
}

int ShapedItem::cutEnd(int offset, int tillOffset) const {
	const auto attributes = Attributes(*_entry);
	auto result = offset + 1;
	while (result < tillOffset && !attributes[result].graphemeBoundary) {
		++result;
	}
	// A letter drawn as one glyph can not be cut apart, whatever the caret
	// would be allowed to do inside it.
	return std::max(result, clusterEnd(offset, tillOffset));
}

ShapedItem::Hit ShapedItem::hitTest(
		Fixed x,
		int fromOffset,
		int tillOffset) const {
	// Pango answers this question itself, but it divides a cluster between
	// its code points, and a caret does not go between them: an emoji is two
	// of them, a letter with a mark above it is two of them as well, and both
	// are one place to stand. So the division is done here over the same
	// letters the other backend uses, and by the same steps.
	const auto attributes = Attributes(*_entry);
	auto position = _rtl ? width(fromOffset, tillOffset) : Fixed();
	auto letters = QVarLengthArray<int, 16>();
	for (auto ch = fromOffset; ch < tillOffset;) {
		// A caret only goes before or after a whole visible letter, and
		// letters that share a glyph - a ligature, a consonant with its matra
		// - have no width of their own, so they take an equal share of the one
		// they are drawn as, which is how a point maps back to a position.
		const auto clusterEnd = this->clusterEnd(ch, tillOffset);
		const auto clusterWidth = width(ch, clusterEnd);
		letters.clear();
		for (auto i = ch; i != clusterEnd; ++i) {
			if (attributes[i].graphemeBoundary) {
				letters.push_back(i);
			}
		}
		if (letters.isEmpty()) {
			letters.push_back(ch);
		}
		// Every boundary is measured from where the cluster begins. Adding an
		// equal share letter by letter would drop what the division leaves
		// over, and that remainder would push everything after it.
		const auto count = int(letters.size());
		const auto origin = position;
		const auto at = [&](int part, int of) {
			const auto shift = clusterWidth * part / of;
			return _rtl ? (origin - shift) : (origin + shift);
		};
		for (auto k = 0; k != count; ++k) {
			const auto from = letters[k];
			const auto till = (k + 1 != count) ? letters[k + 1] : clusterEnd;
			const auto edge = at(k + 1, count);
			const auto inside = _rtl ? (x >= edge) : (x < edge);
			if (inside) {
				const auto middle = at(2 * k + 1, 2 * count);
				const auto before = _rtl ? (x >= middle) : (x < middle);
				return { before ? from : (till - 1), !before };
			}
		}
		position = _rtl ? (origin - clusterWidth) : (origin + clusterWidth);
		ch = clusterEnd;
	}
	return (tillOffset > fromOffset)
		? Hit{ tillOffset - 1, true }
		: Hit{ fromOffset, false };
}

bool ShapedItem::invisibleAt(int offset) const {
	const auto &entry = *_entry;
	if (offset < 0 || offset >= entry.length) {
		return false;
	}
	const auto glyph = GlyphAt(entry, offset);
	return (glyph >= 0)
		&& (entry.glyphs->glyphs[glyph].glyph == PANGO_GLYPH_EMPTY);
}

Fixed ShapedItem::rightBearingBefore(int offset) const {
	const auto &entry = *_entry;
	if (offset <= 0 || offset > entry.length) {
		return {};
	}
	const auto glyph = GlyphAt(entry, offset - 1);
	if (glyph < 0) {
		return {};
	}
	auto ink = PangoRectangle();
	auto logical = PangoRectangle();
	pango_font_get_glyph_extents(
		entry.item->analysis.font,
		entry.glyphs->glyphs[glyph].glyph,
		&ink,
		&logical);
	// How much room is left between where the glyph ends and where the next
	// one starts. It goes negative when the glyph reaches past its own
	// advance, and only that case is interesting: the layout has to keep room
	// for the ink, or the text is drawn outside the place that was measured
	// for it.
	const auto bearing = logical.width - (ink.x + ink.width);
	return (bearing < 0) ? FromPango(bearing, entry.ratio) : Fixed();
}

void ShapedItem::draw(
		QPainter &p,
		QPointF at,
		int fromOffset,
		int tillOffset,
		const QFont &font) const {
	if (fromOffset >= tillOffset) {
		return;
	}
	const auto &entry = *_entry;
	const auto item = entry.item;
	const auto from = entry.text->toUtf8(entry.position + fromOffset)
		- item->offset;
	const auto till = entry.text->toUtf8(entry.position + tillOffset)
		- item->offset;

	// Only the glyphs of the asked range, so that a cut item draws its part
	// and nothing of what was cut away - and the item itself where the whole
	// of it is asked for, which is what a line that was not cut does.
	const auto whole = (fromOffset <= 0) && (tillOffset >= entry.length);
	const auto part = whole ? entry.glyphs : pango_glyph_string_new();
	const auto guard = gsl::finally([&] {
		if (!whole) {
			pango_glyph_string_free(part);
		}
	});
	for (auto i = 0; !whole && i != entry.glyphs->num_glyphs; ++i) {
		const auto cluster = entry.glyphs->log_clusters[i];
		if (cluster >= from && cluster < till) {
			const auto index = part->num_glyphs;
			pango_glyph_string_set_size(part, index + 1);
			part->glyphs[index] = entry.glyphs->glyphs[i];
			part->log_clusters[index] = cluster - from;
		}
	}
	if (!part->num_glyphs) {
		return;
	}

	auto ink = PangoRectangle();
	auto logical = PangoRectangle();
	pango_glyph_string_extents(part, item->analysis.font, &ink, &logical);
	if (ink.width <= 0 || ink.height <= 0) {
		return;
	}

	// Subpixel antialiasing measures coverage per colour channel, and that
	// can only be blended against a background it can see - which is why Qt
	// keeps a three channel mask and blends it into the destination instead
	// of flattening it into an image of its own. So when the destination can
	// be drawn into directly, it is; the copy below is for when it can not,
	// and there the antialiasing has to be plain grey.
	const auto ratio = entry.ratio;

	// Straight into the buffer of the painter where that is possible, and
	// through an image of ours where it is not - and either way what the
	// font asks to be drawn over the letters comes after, below.
	if (!drawInPlace(p, at, item->analysis.font, part)) {
		// What the painter turns the text by, without the scale by the ratio
		// of the device that the glyphs were shaped in - the glyphs go through
		// it here as well, because an image given to the painter would be
		// stretched by it instead. A projection is not a matrix cairo has, so
		// there the image is what gets turned, which is the old way.
		const auto full = p.deviceTransform();
		const auto rest = QTransform::fromScale(1. / ratio, 1. / ratio) * full;
		const auto turn = (full.type() < QTransform::TxProject)
			? QTransform(rest.m11(), rest.m12(), rest.m21(), rest.m22(), 0, 0)
			: QTransform();

		// A glyph may reach outside its advance in every direction, so the
		// ink is what the image has to hold, and where it sits places it.
		const auto inked = turn.mapRect(QRectF(
			PANGO_PIXELS_FLOOR(ink.x),
			PANGO_PIXELS_FLOOR(ink.y),
			PANGO_PIXELS_CEIL(ink.x + ink.width) - PANGO_PIXELS_FLOOR(ink.x),
			PANGO_PIXELS_CEIL(ink.y + ink.height) - PANGO_PIXELS_FLOOR(ink.y)));
		const auto left = qFloor(inked.x());
		const auto top = qFloor(inked.y());
		const auto width = qCeil(inked.x() + inked.width()) - left;
		const auto height = qCeil(inked.y() + inked.height()) - top;

		// In pixels of the device, which is what Pango was asked in.
		auto image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);

		const auto surface = cairo_image_surface_create_for_data(
			image.bits(),
			CAIRO_FORMAT_ARGB32,
			width,
			height,
			int(image.bytesPerLine()));
		const auto context = cairo_create(surface);

		// Subpixel antialiasing measures coverage per colour channel, and an image
		// keeping one alpha per pixel has nowhere to hold that: put somewhere else
		// afterwards, what was measured per channel turns into a tint. Drawing
		// straight into the buffer of the painter is what it takes to keep it, so
		// here the glyphs are asked for grey - of the font, because that is where
		// the answer is kept.
		const auto color = p.pen().color();
		cairo_set_source_rgba(
			context,
			color.redF(),
			color.greenF(),
			color.blueF(),
			color.alphaF());
		cairo_translate(context, -left, -top);
		const auto turning = cairo_matrix_t{
			.xx = turn.m11(),
			.yx = turn.m12(),
			.xy = turn.m21(),
			.yy = turn.m22(),
		};
		cairo_transform(context, &turning);
		cairo_move_to(context, 0, 0);
		ShowGlyphs(context, item->analysis.font, part, false);
		cairo_destroy(context);
		cairo_surface_destroy(surface);
		image.setDevicePixelRatio(ratio);

		// Where the image goes is counted in the pixels of the device, because
		// the turn of the painter is in the glyphs already - so it is put there
		// with only what places the painter itself left in the way.
		const auto place = QPointF(full.map(at)) + QPointF(left, top);
		auto invertible = false;
		p.save();
		p.setWorldTransform(QTransform());
		const auto base = p.deviceTransform().inverted(&invertible);
		if (invertible) {
			p.drawImage(base.map(place), image);
		}
		p.restore();

		// Straight lines, drawn by the painter rather than rasterized: there is no
		// glyph in them, so nothing about them belongs to a font engine, and where
		// they go is what the font says.
	}

	if (!font.underline() && !font.strikeOut()) {
		return;
	}
	const auto metrics = pango_font_get_metrics(item->analysis.font, nullptr);
	const auto guardMetrics = gsl::finally([&] {
		pango_font_metrics_unref(metrics);
	});
	const auto advance = FromPango(
		XAt(entry, tillOffset) - XAt(entry, fromOffset),
		ratio);
	const auto line = [&](int position, int thickness) {
		const auto height = std::max(
			FromPango(thickness, ratio).toReal(),
			1. / ratio);
		p.fillRect(
			QRectF(
				at.x() + std::min(0., _rtl ? -advance.toReal() : 0.),
				at.y() - FromPango(position, ratio).toReal(),
				std::abs(advance.toReal()),
				height),
			p.pen().brush());
	};
	if (font.underline()) {
		line(
			pango_font_metrics_get_underline_position(metrics),
			pango_font_metrics_get_underline_thickness(metrics));
	}
	if (font.strikeOut()) {
		line(
			pango_font_metrics_get_strikethrough_position(metrics),
			pango_font_metrics_get_strikethrough_thickness(metrics));
	}
}

} // namespace Ui::Text
