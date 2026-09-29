// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/basic_click_handlers.h"

#include "ui/widgets/tooltip.h"
#include "ui/text/text_entity.h"
#include "ui/integration.h"
#include "base/qthelp_url.h"
#include "base/qt/qt_string_view.h"

#include <QtCore/QUrl>
#include <QtCore/QRegularExpression>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

namespace {

[[nodiscard]] bool IsAsciiLetter(uint ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

[[nodiscard]] bool IsAsciiDigitOrHyphen(uint ch) {
	return (ch >= '0' && ch <= '9') || (ch == '-');
}

[[nodiscard]] bool IsAscii(QStringView text) {
	return ranges::all_of(text, [](QChar ch) { return ch.unicode() < 0x80; });
}

[[nodiscard]] bool IsPlainAsciiLabel(QStringView label) {
	return ranges::all_of(label, [](QChar ch) {
		return IsAsciiLetter(ch.unicode())
			|| IsAsciiDigitOrHyphen(ch.unicode());
	});
}

// Script_Common for a label without letters, Script_Unknown if mixed.
[[nodiscard]] QChar::Script DomainLabelScript(QStringView label) {
	auto result = QChar::Script_Common;
	for (const auto ch : label.toUcs4()) {
		if (IsAsciiDigitOrHyphen(ch)) {
			continue;
		}
		const auto script = QChar::script(ch);
		if (script == QChar::Script_Common
			|| script == QChar::Script_Inherited
			|| script == QChar::Script_Unknown
			|| (result != QChar::Script_Common && result != script)) {
			return QChar::Script_Unknown;
		}
		result = script;
	}
	return result;
}

} // namespace

QString TextClickHandler::readable() const {
	const auto result = url();
	const auto external = UrlClickHandler::ExternalUrlFromInternalUrl(result);
	return !external.isEmpty()
		? external
		: !result.startsWith(u"internal:"_q)
		? result
		: QString();
}

UrlClickHandler::UrlClickHandler(const QString &url, bool fullDisplayed)
: TextClickHandler(fullDisplayed)
, _originalUrl(url) {
	if (isEmail()) {
		_readable = _originalUrl;
	} else if (const auto external = ExternalUrlFromInternalUrl(_originalUrl);
			!external.isEmpty()) {
		const auto original = QUrl(external);
		const auto good = QUrl(original.isValid()
			? original.toEncoded()
			: QString());
		_readable = good.isValid() ? good.toDisplayString() : external;
	} else if (!_originalUrl.startsWith(u"internal:"_q)) {
		const auto original = QUrl(_originalUrl);
		const auto good = QUrl(original.isValid()
			? original.toEncoded()
			: QString());
		_readable = good.isValid() ? good.toDisplayString() : _originalUrl;
	}
}

QString UrlClickHandler::copyToClipboardContextItemText() const {
	return isEmail()
		? Ui::Integration::Instance().phraseContextCopyEmail()
		: Ui::Integration::Instance().phraseContextCopyLink();
}

QString UrlClickHandler::EncodeForOpening(const QString &originalUrl) {
	if (IsEmail(originalUrl)) {
		return originalUrl;
	}

	static const auto TonExp = QRegularExpression(u"^[^/@:]+\\.ton($|/)"_q);
	if (TonExp.match(originalUrl.toLower()).hasMatch()) {
		return u"tonsite://"_q + originalUrl;
	}

	const auto u = QUrl(originalUrl);
	const auto good = QUrl(u.isValid() ? u.toEncoded() : QString());
	const auto result = good.isValid()
		? QString::fromUtf8(good.toEncoded())
		: originalUrl;

	static const auto RegExp = QRegularExpression(u"^[a-zA-Z]+:"_q);

	if (!result.isEmpty()
		&& !RegExp.match(result).hasMatch()) {
		// No protocol.
		return u"https://"_q + result;
	}
	return result;
}

QString UrlClickHandler::EncodeInternalWrappedUrl(
		const QString &url,
		const QString &extraQuery) {
	auto result = u"internal:wrapped?url=%1"_q.arg(qthelp::url_encode(url));
	if (!extraQuery.isEmpty()) {
		result += '&' + extraQuery;
	}
	return result;
}

QString UrlClickHandler::ExternalUrlFromInternalUrl(const QString &url) {
	const auto wrappedPrefix = u"internal:wrapped?"_q;
	if (!url.startsWith(wrappedPrefix)) {
		return QString();
	}
	return qthelp::url_parse_params(
		url.mid(wrappedPrefix.size())
	).value(u"url"_q);
}

void UrlClickHandler::Open(QString url, QVariant context) {
	Ui::Tooltip::Hide();
	if (!Ui::Integration::Instance().handleUrlClick(url, context)
		&& !url.isEmpty()) {
		if (IsEmail(url)) {
			url = "mailto: " + url;
		}
		QDesktopServices::openUrl(url);
	}
}

bool UrlClickHandler::IsSuspicious(const QString &url) {
	return !SuspiciousRanges(url).empty();
}

auto UrlClickHandler::SuspiciousRanges(const QString &url)
-> std::vector<SuspiciousRange> {
	static const auto Check1 = QRegularExpression(
		"^((https?|s?ftp)://)?([^/#\\:\\?]+)([/#\\:\\?]|$)",
		QRegularExpression::CaseInsensitiveOption);
	const auto match1 = Check1.match(url);
	if (!match1.hasMatch()) {
		return {};
	}
	const auto domain = match1.capturedView(3);
	static const auto Check2 = QRegularExpression("^(.*)\\.[a-zA-Z]+$");
	const auto latinTld = Check2.match(domain).hasMatch();
	if (!latinTld && IsAscii(domain)) {
		return {};
	}
	const auto script = latinTld
		? QChar::Script_Latin
		: DomainLabelScript(domain.mid(domain.lastIndexOf('.') + 1));
	const auto asciiOnly = (script == QChar::Script_Latin)
		|| (script == QChar::Script_Common)
		|| (script == QChar::Script_Unknown);
	const auto allowed = [&](uint ch) {
		return IsAsciiDigitOrHyphen(ch)
			|| (asciiOnly ? IsAsciiLetter(ch) : (QChar::script(ch) == script));
	};
	auto result = std::vector<SuspiciousRange>();
	const auto offset = int(match1.capturedStart(3));
	for (const auto &label : domain.split(QChar('.'))) {
		if (!asciiOnly && IsPlainAsciiLabel(label)) {
			continue;
		}
		const auto start = offset + int(label.data() - domain.data());
		for (auto i = 0, size = int(label.size()); i != size;) {
			const auto pair = label[i].isHighSurrogate()
				&& (i + 1 < size)
				&& label[i + 1].isLowSurrogate();
			const auto length = pair ? 2 : 1;
			const auto ch = pair
				? uint(QChar::surrogateToUcs4(label[i], label[i + 1]))
				: uint(label[i].unicode());
			if (!allowed(ch)) {
				const auto from = start + i;
				if (!result.empty()
					&& (result.back().from + result.back().length == from)) {
					result.back().length += length;
				} else {
					result.push_back({ .from = from, .length = length });
				}
			}
			i += length;
		}
	}
	return result;
}


QString UrlClickHandler::ShowEncoded(const QString &url) {
	if (const auto u = QUrl(url); u.isValid()) {
		return QString::fromUtf8(u.toEncoded());
	}
	static const auto Check1 = QRegularExpression(
		"^(https?://)?([^/#\\:]+)([/#\\:]|$)",
		QRegularExpression::CaseInsensitiveOption);
	if (const auto match1 = Check1.match(url); match1.hasMatch()) {
		const auto domain = match1.captured(1).append(match1.capturedView(2));
		if (const auto u = QUrl(domain); u.isValid()) {
			return QString(
			).append(QString::fromUtf8(u.toEncoded())
			).append(base::StringViewMid(url, match1.capturedEnd(2)));
		}
	}
	return url;
}

auto UrlClickHandler::getTextEntity() const -> TextEntity {
	const auto type = isEmail() ? EntityType::Email : EntityType::Url;
	return { type, _originalUrl };
}
