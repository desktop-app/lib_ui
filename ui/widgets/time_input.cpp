// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/time_input.h"

#include "ui/widgets/input_fields.h"
#include "ui/ui_utility.h"
#include "base/qt_adapters.h"

#include <QtCore/QRegularExpression>
#include <QTime>

namespace Ui {
namespace {

QTime ValidateTime(const QString &value) {
	const auto match = QRegularExpression(
		"^(\\d{1,2})\\:(\\d\\d)$").match(value);
	if (!match.hasMatch()) {
		return QTime();
	}
	const auto readInt = [](const QString &value) {
		auto view = QStringView(value);
		while (!view.isEmpty() && view.at(0) == '0') {
			view = base::StringViewMid(view, 1);
		}
		return view.toInt();
	};
	return QTime(readInt(match.captured(1)), readInt(match.captured(2)));
}

QString GetHour(const QString &value) {
	if (const auto time = ValidateTime(value); time.isValid()) {
		return QString::number(time.hour());
	}
	return QString();
}

QString GetMinute(const QString &value) {
	if (const auto time = ValidateTime(value); time.isValid()) {
		return QString("%1").arg(time.minute(), 2, 10, QChar('0'));
	}
	return QString();
}

} // namespace

class TimePart final : public MaskedInputField {
public:
	using MaskedInputField::MaskedInputField;

	void setMaxValue(int value);
	void setWheelStep(int value);

	rpl::producer<> erasePrevious() const;
	rpl::producer<QChar> putNext() const;

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
	rpl::event_stream<QChar> _putNext;

};

std::optional<int> Number(not_null<TimePart*> field) {
	const auto text = field->getLastText();
	auto view = QStringView(text);
	while (view.size() > 1 && view.at(0) == '0') {
		view = base::StringViewMid(view, 1);
	}
	return QRegularExpression("^\\d+$").match(view).hasMatch()
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

rpl::producer<QChar> TimePart::putNext() const {
	return _putNext.events();
}

void TimePart::keyPressEvent(QKeyEvent *e) {
	const auto isBackspace = (e->key() == Qt::Key_Backspace);
	const auto isBeginning = (cursorPosition() == 0);
	if (isBackspace && isBeginning && !hasSelectedText()) {
		_erasePrevious.fire({});
	} else {
		MaskedInputField::keyPressEvent(e);
	}
}

void TimePart::wheelEvent(QWheelEvent *e) {
	const auto direction = WheelDirection(e);
	const auto now = Number(this);
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
}

void TimePart::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto newText = QString();
	auto newCursor = -1;
	const auto oldCursor = nowCursor;
	const auto oldLength = now.size();
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

TimeInput::TimeInput(
	QWidget *parent,
	const QString &value,
	const style::InputField &stField,
	const style::InputField &stDateField,
	const style::FlatLabel &stSeparator,
	const style::margins &stSeparatorPadding)
: RpWidget(parent)
, _stField(stField)
, _stDateField(stDateField)
, _stSeparator(stSeparator)
, _stSeparatorPadding(stSeparatorPadding)
, _hour(
	this,
	_stField,
	rpl::never<QString>(),
	GetHour(value))
, _separator1(
	this,
	object_ptr<FlatLabel>(
		this,
		QString(":"),
		_stSeparator),
	_stSeparatorPadding)
, _minute(
	this,
	_stField,
	rpl::never<QString>(),
	GetMinute(value))
, _value(valueCurrent()) {
	const auto focused = [=](const object_ptr<TimePart> &field) {
		return [this, pointer = MakeWeak(field.data())]{
			_borderAnimationStart = pointer->borderAnimationStart()
				+ pointer->x()
				- _hour->x();
			setFocused(true);
			_focuses.fire({});
		};
	};
	const auto blurred = [=] {
		setFocused(false);
	};
	const auto changed = [=] {
		_value = valueCurrent();
	};
	connect(_hour, &MaskedInputField::focused, focused(_hour));
	connect(_minute, &MaskedInputField::focused, focused(_minute));
	connect(_hour, &MaskedInputField::blurred, blurred);
	connect(_minute, &MaskedInputField::blurred, blurred);
	connect(_hour, &MaskedInputField::changed, changed);
	connect(_minute, &MaskedInputField::changed, changed);
	_hour->setMaxValue(23);
	_hour->setWheelStep(1);
	_hour->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(_minute, ch);
	}, lifetime());
	_minute->setMaxValue(59);
	_minute->setWheelStep(10);
	_minute->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(_hour);
	}, lifetime());
	_separator1->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);

	_value.changes(
	) | rpl::start_with_next([=] {
		setErrorShown(false);
	}, lifetime());

	const auto submitHour = [=] {
		if (hour().has_value()) {
			_minute->setFocus();
		}
	};
	const auto submitMinute = [=] {
		if (minute().has_value()) {
			if (hour().has_value()) {
				_submitRequests.fire({});
			} else {
				_hour->setFocus();
			}
		}
	};
	connect(
		_hour,
		&MaskedInputField::submitted,
		submitHour);
	connect(
		_minute,
		&MaskedInputField::submitted,
		submitMinute);
}

void TimeInput::putNext(const object_ptr<TimePart> &field, QChar ch) {
	field->setCursorPosition(0);
	if (ch.unicode()) {
		field->setText(ch + field->getLastText());
		field->setCursorPosition(1);
	}
	field->setFocus();
}

void TimeInput::erasePrevious(const object_ptr<TimePart> &field) {
	const auto text = field->getLastText();
	if (!text.isEmpty()) {
		field->setCursorPosition(text.size() - 1);
		field->setText(text.mid(0, text.size() - 1));
	}
	field->setFocus();
}

bool TimeInput::setFocusFast() {
	if (hour().has_value()) {
		_minute->setFocusFast();
	} else {
		_hour->setFocusFast();
	}
	return true;
}

std::optional<int> TimeInput::hour() const {
	return Number(_hour);
}

std::optional<int> TimeInput::minute() const {
	return Number(_minute);
}

QString TimeInput::valueCurrent() const {
	const auto result = QString("%1:%2"
		).arg(hour().value_or(0)
		).arg(minute().value_or(0), 2, 10, QChar('0'));
	return ValidateTime(result).isValid() ? result : QString();
}

rpl::producer<QString> TimeInput::value() const {
	return _value.value();
}

rpl::producer<> TimeInput::submitRequests() const {
	return _submitRequests.events();
}

rpl::producer<> TimeInput::focuses() const {
	return _focuses.events();
}

void TimeInput::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto &_st = _stDateField;
	const auto height = _st.heightMin;
	if (_st.border) {
		p.fillRect(0, height - _st.border, width(), _st.border, _st.borderFg);
	}
	auto errorDegree = _a_error.value(_error ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.value(1.);
	auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = std::clamp(_borderAnimationStart, 0, width());
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width() - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}
}

template <typename Widget>
bool TimeInput::insideSeparator(QPoint position, const Widget &widget) const {
	const auto x = position.x();
	const auto y = position.y();
	return (x >= widget->x() && x < widget->x() + widget->width())
		&& (y >= _hour->y() && y < _hour->y() + _hour->height());
}

void TimeInput::mouseMoveEvent(QMouseEvent *e) {
	const auto cursor = insideSeparator(e->pos(), _separator1)
		? style::cur_text
		: style::cur_default;
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

void TimeInput::mousePressEvent(QMouseEvent *e) {
	const auto x = e->pos().x();
	const auto focus1 = [&] {
		if (_hour->getLastText().size() > 1) {
			_minute->setFocus();
		} else {
			_hour->setFocus();
		}
	};
	if (insideSeparator(e->pos(), _separator1)) {
		focus1();
		_borderAnimationStart = x - _hour->x();
	}
}

int TimeInput::resizeGetHeight(int width) {
	const auto &_st = _stField;
	const auto &font = _st.placeholderFont;
	const auto addToWidth = _stSeparatorPadding.left();
	const auto hourWidth = _st.textMargins.left()
		+ _st.placeholderMargins.left()
		+ font->width(QString("23"))
		+ _st.placeholderMargins.right()
		+ _st.textMargins.right()
		+ addToWidth;
	const auto minuteWidth = _st.textMargins.left()
		+ _st.placeholderMargins.left()
		+ font->width(QString("59"))
		+ _st.placeholderMargins.right()
		+ _st.textMargins.right()
		+ addToWidth;
	const auto full = hourWidth
		- addToWidth
		+ _separator1->width()
		+ minuteWidth
		- addToWidth;
	auto left = (width - full) / 2;
	auto top = 0;
	_hour->setGeometry(left, top, hourWidth, _hour->height());
	left += hourWidth - addToWidth;
	_separator1->resizeToNaturalWidth(width);
	_separator1->move(left, top);
	left += _separator1->width();
	_minute->setGeometry(left, top, minuteWidth, _minute->height());
	return _stDateField.heightMin;
}

void TimeInput::showError() {
	setErrorShown(true);
	if (!_focused) {
		setInnerFocus();
	}
}

void TimeInput::setInnerFocus() {
	if (hour().has_value()) {
		_minute->setFocus();
	} else {
		_hour->setFocus();
	}
}

void TimeInput::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start(
			[=] { update(); },
			_error ? 0. : 1.,
			_error ? 1. : 0.,
			_stDateField.duration);
		startBorderAnimation();
	}
}

void TimeInput::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start(
			[=] { update(); },
			_focused ? 0. : 1.,
			_focused ? 1. : 0.,
			_stDateField.duration);
		startBorderAnimation();
	}
}

void TimeInput::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		const auto duration = _stDateField.duration;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([=] { update(); }, 0., 1., duration);
			} else {
				_a_borderShown.start([=] { update(); }, 0., 1., duration);
			}
		} else {
			_a_borderOpacity.start([=] { update(); }, 1., 0., duration);
		}
	}
}

} // namespace Ui
