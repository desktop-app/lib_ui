// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
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
