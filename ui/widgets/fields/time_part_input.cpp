// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/fields/time_part_input.h"

#include "base/qt/qt_string_view.h"
#include "ui/ui_utility.h" // WheelDirection

#include <QtCore/QRegularExpression>

namespace Ui {

std::optional<int> TimePart::number() {
	static const auto RegExp = QRegularExpression("^\\d+$");
	const auto text = getLastText();
	auto view = QStringView(text);
	while (view.size() > 1 && view.at(0) == '0') {
		view = base::StringViewMid(view, 1);
	}
	return RegExp.match(view).hasMatch()
		? std::make_optional(view.toInt())
		: std::nullopt;
}

void TimePart::setMaxValue(int value) {
	_maxValue = value;
	_maxDigits = 0;
	while (value > 0) {
		++_maxDigits;
		value /= 10;
	}
}

void TimePart::setWheelStep(int value) {
	_wheelStep = value;
}

rpl::producer<> TimePart::erasePrevious() const {
	return _erasePrevious.events();
}

rpl::producer<> TimePart::jumpToPrevious() const {
	return _jumpToPrevious.events();
}

rpl::producer<QChar> TimePart::putNext() const {
	return _putNext.events();
}

void TimePart::keyPressEvent(QKeyEvent *e) {
	const auto position = cursorPosition();
	const auto selection = hasSelectedText();
	if (!selection && !position) {
		if (e->key() == Qt::Key_Backspace) {
			_erasePrevious.fire({});
			return;
		} else if (e->key() == Qt::Key_Left) {
			_jumpToPrevious.fire({});
			return;
		}
	} else if (!selection && position == getLastText().size()) {
		if (e->key() == Qt::Key_Right) {
			_putNext.fire(QChar(0));
			return;
		}
	}
	MaskedInputField::keyPressEvent(e);
}

void TimePart::wheelEvent(QWheelEvent *e) {
	const auto direction = WheelDirection(e);
	const auto now = number();
	if (!now.has_value()) {
		return;
	}
	auto time = *now + (direction * _wheelStep);
	const auto max = _maxValue + 1;
	if (time < 0) {
		time += max;
	} else if (time >= max) {
		time -= max;
	}
	setText(QString::number(time));
	Ui::MaskedInputField::changed();
}

void TimePart::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	const auto oldCursor = nowCursor;
	const auto oldLength = now.size();
	auto newCursor = (oldCursor > 0) ? -1 : 0;
	auto newText = QString();
	auto accumulated = 0;
	auto limit = 0;
	for (; limit != oldLength; ++limit) {
		if (now[limit].isDigit()) {
			accumulated *= 10;
			accumulated += (now[limit].unicode() - '0');
			if (accumulated > _maxValue || limit == _maxDigits) {
				break;
			}
		}
	}
	for (auto i = 0; i != limit;) {
		if (now[i].isDigit()) {
			newText += now[i];
		}
		if (++i == oldCursor) {
			newCursor = newText.size();
		}
	}
	if (newCursor < 0) {
		newCursor = newText.size();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	if (newCursor != nowCursor) {
		nowCursor = newCursor;
		setCursorPosition(nowCursor);
	}
	if (accumulated > _maxValue
		|| (limit == _maxDigits && oldLength > _maxDigits)) {
		if (oldCursor > limit) {
			_putNext.fire('0' + (accumulated % 10));
		} else {
			_putNext.fire(0);
		}
	}
}

} // namespace Ui
