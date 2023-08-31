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

class PasswordInput final : public MaskedInputField {
public:
	PasswordInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder = nullptr,
		const QString &val = QString());

};

} // namespace Ui
