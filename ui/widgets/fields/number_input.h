// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/fields/masked_input_field.h"

namespace style {
struct InputField;
} // namespace style

namespace Ui {

class NumberInput final : public MaskedInputField {
public:
	NumberInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &value,
		int limit);

	void changeLimit(int limit);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	int _limit = 0;

};

} // namespace Ui
