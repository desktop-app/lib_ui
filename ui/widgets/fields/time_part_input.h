// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/fields/masked_input_field.h"

namespace Ui {

class TimePart : public MaskedInputField {
public:
	using MaskedInputField::MaskedInputField;

	void setMaxValue(int value);
	void setWheelStep(int value);

	[[nodiscard]] rpl::producer<> erasePrevious() const;
	[[nodiscard]] rpl::producer<> jumpToPrevious() const;
	[[nodiscard]] rpl::producer<QChar> putNext() const;

	[[nodiscard]] std::optional<int> number();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	int _maxValue = 0;
	int _maxDigits = 0;
	int _wheelStep = 0;
	rpl::event_stream<> _erasePrevious;
	rpl::event_stream<> _jumpToPrevious;
	rpl::event_stream<QChar> _putNext;

};

} // namespace Ui
