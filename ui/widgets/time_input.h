// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"

#include "ui/effects/animations.h"
#include "ui/widgets/labels.h"

namespace Ui {

class TimePart;

class TimeInput final : public RpWidget {
public:
	TimeInput(
		QWidget *parent,
		const QString &value,
		const style::InputField &stField,
		const style::InputField &stDateField,
		const style::FlatLabel &stSeparator,
		const style::margins &stSeparatorPadding);

	bool setFocusFast();
	rpl::producer<QString> value() const;
	rpl::producer<> submitRequests() const;
	rpl::producer<> focuses() const;
	QString valueCurrent() const;
	void showError();

	int resizeGetHeight(int width) override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void setInnerFocus();
	void putNext(const object_ptr<TimePart> &field, QChar ch);
	void erasePrevious(const object_ptr<TimePart> &field);
	void setErrorShown(bool error);
	void setFocused(bool focused);
	void startBorderAnimation();
	template <typename Widget>
	bool insideSeparator(QPoint position, const Widget &widget) const;

	int hour() const;
	int minute() const;

	const style::InputField &_stField;
	const style::InputField &_stDateField;
	const style::FlatLabel &_stSeparator;
	const style::margins &_stSeparatorPadding;

	object_ptr<TimePart> _hour;
	object_ptr<PaddingWrap<FlatLabel>> _separator1;
	object_ptr<TimePart> _minute;
	rpl::variable<QString> _value;
	rpl::event_stream<> _submitRequests;
	rpl::event_stream<> _focuses;

	style::cursor _cursor = style::cur_default;
	Animations::Simple _a_borderShown;
	int _borderAnimationStart = 0;
	Animations::Simple _a_borderOpacity;
	bool _borderVisible = false;

	Animations::Simple _a_error;
	bool _error = false;
	Animations::Simple _a_focused;
	bool _focused = false;

};

} // namespace Ui
