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

#include <QtCore/QUrl>
#include <QtCore/QRegularExpression>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

QString TextClickHandler::readable() const {
	const auto result = url();
	return result.startsWith(qstr("internal:")) ? QString() : result;
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
	}
}

QString UrlClickHandler::copyToClipboardContextItemText() const {
	return isEmail()
		? Ui::Integration::Instance().phraseContextCopyEmail()
		: Ui::Integration::Instance().phraseContextCopyLink();
}

QString UrlClickHandler::url() const {
	if (isEmail()) {
		return _originalUrl;
	}

	QUrl u(_originalUrl), good(u.isValid() ? u.toEncoded() : QString());
	QString result(good.isValid() ? QString::fromUtf8(good.toEncoded()) : _originalUrl);

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
		QDesktopServices::openUrl(url);
	}
}

auto UrlClickHandler::getTextEntity() const -> TextEntity {
	const auto type = isEmail() ? EntityType::Email : EntityType::Url;
	return { type, _originalUrl };
}
