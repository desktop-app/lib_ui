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

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "base/platform/linux/base_linux_app_launch_context.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#include <QtCore/QUrl>
#include <QtCore/QRegularExpression>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <giomm.h>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

QString TextClickHandler::readable() const {
	const auto result = url();
	return !result.startsWith(qstr("internal:"))
		? result
		: result.startsWith(qstr("internal:url:"))
		? result.mid(qstr("internal:url:").size())
		: QString();
}

UrlClickHandler::UrlClickHandler(const QString &url, bool fullDisplayed)
: TextClickHandler(fullDisplayed)
, _originalUrl(url) {
	if (isEmail()) {
		_readable = _originalUrl;
	} else if (!_originalUrl.startsWith(qstr("internal:"))) {
		const auto original = QUrl(_originalUrl);
		const auto good = QUrl(original.isValid()
			? original.toEncoded()
			: QString());
		_readable = good.isValid() ? good.toDisplayString() : _originalUrl;
	} else if (_originalUrl.startsWith(qstr("internal:url:"))) {
		const auto external = _originalUrl.mid(qstr("internal:url:").size());
		const auto original = QUrl(external);
		const auto good = QUrl(original.isValid()
			? original.toEncoded()
			: QString());
		_readable = good.isValid() ? good.toDisplayString() : external;
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

	const auto u = QUrl(originalUrl);
	const auto good = QUrl(u.isValid() ? u.toEncoded() : QString());
	const auto result = good.isValid()
		? QString::fromUtf8(good.toEncoded())
		: originalUrl;

	if (!result.isEmpty()
		&& !QRegularExpression(
			QStringLiteral("^[a-zA-Z]+:")).match(result).hasMatch()) {
		// No protocol.
		return QStringLiteral("http://") + result;
	}
	return result;
}

void UrlClickHandler::Open(QString url, QVariant context) {
	Ui::Tooltip::Hide();
	if (!Ui::Integration::Instance().handleUrlClick(url, context)
		&& !url.isEmpty()) {
		if (IsEmail(url)) {
			url = "mailto: " + url;
		}
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		// Desktop entry spec implementation,
		// prefer it over QDesktopServices::openUrl since it just calls
		// the xdg-open shell script that is known to be bugged:
		// various shell-specific bugs with spaces in paths,
		// logic bugs violating the spec and etc, etc.
		// Not to mention xdg-open can and will use DE-specific executables
		// that allows various ill people to develop extensions
		// to the standard and differ the behavior across apps.
		// https://specifications.freedesktop.org/desktop-entry-spec/latest/
		try {
			if (Gio::AppInfo::launch_default_for_uri(
				url.toStdString(),
				base::Platform::AppLaunchContext())) {
				return;
			}
		} catch (...) {
		}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
		QDesktopServices::openUrl(url);
	}
}

bool UrlClickHandler::IsSuspicious(const QString &url) {
	static const auto Check1 = QRegularExpression(
		"^((https?|s?ftp)://)?([^/#\\:]+)([/#\\:]|$)",
		QRegularExpression::CaseInsensitiveOption);
	const auto match1 = Check1.match(url);
	if (!match1.hasMatch()) {
		return false;
	}
	const auto domain = match1.capturedView(3);
	static const auto Check2 = QRegularExpression("^(.*)\\.[a-zA-Z]+$");
	const auto match2 = Check2.match(domain);
	if (!match2.hasMatch()) {
		return false;
	}
	const auto part = match2.capturedView(1);
	static const auto Check3 = QRegularExpression("[^a-zA-Z0-9\\.\\-]");
	return Check3.match(part).hasMatch();
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
