// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/fields/custom_field_object.h"

#include "ui/effects/spoiler_mess.h"
#include "ui/text/text.h"
#include "ui/text/text_renderer.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_basic.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QTextEdit>
#include <QtWidgets/QScrollBar>

namespace Ui {
namespace {

constexpr auto kSpoilerHiddenOpacity = 0.5;

using SpoilerRect = InputFieldSpoilerRect;

} // namespace

class FieldSpoilerOverlay final : public RpWidget {
public:
	FieldSpoilerOverlay(
		not_null<InputField*> field,
		Fn<float64()> shown,
		Fn<bool()> paused);

private:
	void paintEvent(QPaintEvent *e) override;

	const not_null<InputField*> _field;
	const Fn<float64()> _shown;
	const Fn<bool()> _paused;
	SpoilerAnimation _animation;

};

FieldSpoilerOverlay::FieldSpoilerOverlay(
	not_null<InputField*> field,
	Fn<float64()> shown,
	Fn<bool()> paused)
: RpWidget(field->rawTextEdit())
, _field(field)
, _shown(std::move(shown))
, _paused(std::move(paused))
, _animation([=] { update(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	show();
}

void FieldSpoilerOverlay::paintEvent(QPaintEvent *e) {
	auto p = std::optional<QPainter>();
	auto topShift = std::optional<int>();
	auto frame = std::optional<SpoilerMessFrame>();
	auto blockquoteBg = std::optional<QColor>();
	const auto clip = e->rect();

	const auto shown = _shown();
	const auto bgOpacity = shown;
	const auto fgOpacity = 1. * shown + kSpoilerHiddenOpacity * (1. - shown);
	for (const auto &rect : _field->_spoilerRects) {
		const auto fill = rect.geometry.intersected(clip);
		if (fill.isEmpty()) {
			continue;
		} else if (!p) {
			p.emplace(this);
			const auto paused = _paused && _paused();
			frame.emplace(
				Text::DefaultSpoilerCache()->lookup(
					st::defaultTextPalette.spoilerFg->c)->frame(
						_animation.index(crl::now(), paused)));
			topShift = -_field->rawTextEdit()->verticalScrollBar()->value();
		}
		if (bgOpacity > 0.) {
			p->setOpacity(bgOpacity);
			if (rect.blockquote && !blockquoteBg) {
				const auto bg = _field->_blockquoteBg;
				blockquoteBg = (bg.alphaF() < 1.)
					? anim::color(
						_field->_st.textBg->c,
						QColor(bg.red(), bg.green(), bg.blue()),
						bg.alphaF())
					: bg;
			}
			p->fillRect(
				fill,
				rect.blockquote ? *blockquoteBg : _field->_st.textBg->c);
		}
		p->setOpacity(fgOpacity);
		const auto shift = QPoint(0, *topShift) - rect.geometry.topLeft();
		FillSpoilerRect(*p, rect.geometry, *frame, shift);
	}
}

CustomFieldObject::CustomFieldObject(
	not_null<InputField*> field,
	Fn<std::any(Fn<void()> repaint)> context,
	Fn<bool()> pausedEmoji,
	Fn<bool()> pausedSpoiler,
	Text::CustomEmojiFactory factory)
: _field(field)
, _context(std::move(context))
, _pausedEmoji(std::move(pausedEmoji))
, _pausedSpoiler(std::move(pausedSpoiler))
, _factory(makeFactory(std::move(factory)))
, _now(crl::now()) {
}

CustomFieldObject::~CustomFieldObject() = default;

void *CustomFieldObject::qt_metacast(const char *iid) {
	if (QLatin1String(iid) == qobject_interface_iid<QTextObjectInterface*>()) {
		return static_cast<QTextObjectInterface*>(this);
	}
	return QObject::qt_metacast(iid);
}

QSizeF CustomFieldObject::intrinsicSize(
		QTextDocument *doc,
		int posInDocument,
		const QTextFormat &format) {
	const auto line = _field->_st.style.font->height;
	if (format.objectType() == InputField::kCollapsedQuoteFormat) {
		const auto &padding = _field->_st.style.blockquote.padding;
		const auto paddings = padding.left() + padding.right();
		const auto skip = 2 * doc->documentMargin();
		const auto height = Text::kQuoteCollapsedLines * line;
		return QSizeF(doc->pageSize().width() - paddings - skip, height);
	}
	const auto size = st::emojiSize * 1.;
	const auto width = size + st::emojiPadding * 2.;
	const auto height = std::max(line * 1., size);
	if (!_skip) {
		const auto emoji = Text::AdjustCustomEmojiSize(st::emojiSize);
		_skip = (st::emojiSize - emoji) / 2;
	}
	return { width, height };
}

void CustomFieldObject::drawObject(
		QPainter *painter,
		const QRectF &rect,
		QTextDocument *doc,
		int posInDocument,
		const QTextFormat &format) {
	if (format.objectType() == InputField::kCollapsedQuoteFormat) {
		const auto left = 0;
		const auto top = 0;
		const auto id = format.property(InputField::kQuoteId).toInt();
		if (const auto i = _quotes.find(id); i != end(_quotes)) {
			i->second.string.draw(*painter, {
				.position = QPoint(left + rect.x(), top + rect.y()),
				.outerWidth = int(base::SafeRound(doc->pageSize().width())),
				.availableWidth = int(std::floor(rect.width())),

				.palette = nullptr,
				.spoiler = Text::DefaultSpoilerCache(),
				.now = _now,
				.pausedEmoji = _pausedEmoji(),
				.pausedSpoiler = _pausedSpoiler(),

				.elisionLines = Text::kQuoteCollapsedLines,
			});
		}
		return;
	}
	const auto id = format.property(InputField::kCustomEmojiId).toULongLong();
	if (!id) {
		return;
	}
	auto i = _emoji.find(id);
	if (i == end(_emoji)) {
		const auto link = format.property(InputField::kCustomEmojiLink);
		const auto data = InputField::CustomEmojiEntityData(link.toString());
		if (auto emoji = _factory(data)) {
			i = _emoji.emplace(id, std::move(emoji)).first;
		}
	}
	if (i == end(_emoji)) {
		return;
	}
	i->second->paint(*painter, {
		.textColor = format.foreground().color(),
		.now = _now,
		.position = QPoint(
			int(base::SafeRound(rect.x())) + st::emojiPadding + _skip,
			int(base::SafeRound(rect.y())) + _skip),
		.paused = _pausedEmoji && _pausedEmoji(),
	});
}

void CustomFieldObject::clearEmoji() {
	_emoji.clear();
}

void CustomFieldObject::clearQuotes() {
	_quotes.clear();
}

std::unique_ptr<RpWidget> CustomFieldObject::createSpoilerOverlay() {
	return std::make_unique<FieldSpoilerOverlay>(
		_field,
		[=] { return _spoilerOpacity.value(_spoilerHidden ? 0. : 1.); },
		_pausedSpoiler);
}

void CustomFieldObject::refreshSpoilerShown(InputFieldTextRange range) {
	auto hidden = false;
	using Range = InputFieldTextRange;
	const auto intersects = [](Range a, Range b) {
		return (a.from < b.till) && (b.from < a.till);
	};
	if (range.till > range.from) {
		const auto check = [&](const std::vector<Range> &list) {
			if (!hidden) {
				for (const auto &spoiler : list) {
					if (intersects(spoiler, range)) {
						hidden = true;
						break;
					}
				}
			}
		};
		check(_field->_spoilerRangesText);
		check(_field->_spoilerRangesEmoji);
	} else {
		auto touchesLeft = false;
		auto touchesRight = false;
		const auto cursor = range.from;
		const auto check = [&](const std::vector<Range> &list) {
			if (!touchesLeft || !touchesRight) {
				for (const auto &spoiler : list) {
					if (spoiler.from <= cursor && spoiler.till >= cursor) {
						if (spoiler.from < cursor) {
							touchesLeft = true;
						}
						if (spoiler.till >= cursor) {
							touchesRight = true;
						}
						if (touchesLeft && touchesRight) {
							break;
						}
					}
				}
			}
		};
		check(_field->_spoilerRangesText);
		check(_field->_spoilerRangesEmoji);
		hidden = touchesLeft && touchesRight;
	}
	if (_spoilerHidden != hidden) {
		_spoilerHidden = hidden;
		_spoilerOpacity.start(
			[=] { _field->update(); },
			hidden ? 1. : 0.,
			hidden ? 0. : 1.,
			st::fadeWrapDuration);
	}
}

void CustomFieldObject::setCollapsedText(int quoteId, TextWithTags text) {
	auto &quote = _quotes[quoteId];
	quote.string.setMarkedText(_field->_st.style, {
		text.text,
		TextUtilities::ConvertTextTagsToEntities(text.tags),
	}, kMarkupTextOptions, _context([=] { _field->update(); }));
	quote.text = std::move(text);
}

const TextWithTags &CustomFieldObject::collapsedText(int quoteId) const {
	if (const auto i = _quotes.find(quoteId); i != end(_quotes)) {
		return i->second.text;
	}
	static const auto kEmpty = TextWithTags();
	return kEmpty;
}

CustomFieldObject::Factory CustomFieldObject::makeFactory(
		Text::CustomEmojiFactory custom) {
	const auto repaint = [field = _field] { field->update(); };
	if (custom) {
		return [=, factory = std::move(custom)](QStringView data) {
			return factory(data, repaint);
		};
	}
	return [=, context = _context](QStringView data) {
		auto &instance = Integration::Instance();
		return instance.createCustomEmoji(data, context(repaint));
	};
}

void CustomFieldObject::setNow(crl::time now) {
	_now = now;
}

} // namespace Ui
