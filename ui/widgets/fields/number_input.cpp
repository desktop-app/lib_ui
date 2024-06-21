// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/fields/number_input.h"

namespace Ui {

NumberInput::NumberInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &value,
	int limit)
: MaskedInputField(parent, st, std::move(placeholder), value)
, _limit(limit) {
	if (!value.toInt() || (limit > 0 && value.toInt() > limit)) {
		setText(QString());
	}
}

void NumberInput::changeLimit(int limit) {
	_limit = limit;
}

void NumberInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = int(now.size()); i < l; ++i) {
		if (now.at(i).isDigit()) {
			newText.append(now.at(i));
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	if (!newText.toInt()) {
		newText = QString();
		newPos = 0;
	} else if (_limit > 0 && newText.toInt() > _limit) {
		newText = was;
		newPos = wasCursor;
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

} // namespace Ui
