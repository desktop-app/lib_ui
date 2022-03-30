/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {

class IconButtonWithText final : public Ui::IconButton {
public:
	IconButtonWithText(
		not_null<RpWidget*> parent,
		const style::IconButtonWithText &st);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::IconButtonWithText &_st;
	QString _text;

};

} // namespace Ui
