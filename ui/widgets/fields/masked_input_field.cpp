// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/fields/masked_input_field.h"

#include "base/qt/qt_common_adapters.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QCommonStyle>
#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Ui {
namespace {

class InputStyle final : public QCommonStyle {
public:
	InputStyle() {
		setParent(QCoreApplication::instance());
	}

	void drawPrimitive(
		PrimitiveElement element,
		const QStyleOption *option,
		QPainter *painter,
		const QWidget *widget = nullptr) const override {
	}

	static InputStyle *instance() {
		if (!_instance) {
			if (!QGuiApplication::instance()) {
				return nullptr;
			}
			_instance = new InputStyle();
		}
		return _instance;
	}

	~InputStyle() {
		_instance = nullptr;
	}

private:
	static InputStyle *_instance;

};

InputStyle *InputStyle::_instance = nullptr;

} // namespace

MaskedInputField::MaskedInputField(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: Parent(val, parent)
, _st(st)
, _oldtext(val)
, _placeholderFull(std::move(placeholder)) {
	resize(_st.width, _st.heightMin);

	setFont(_st.style.font);
	setAlignment(_st.textAlign);

	_placeholderFull.value(
	) | rpl::start_with_next([=](const QString &text) {
		refreshPlaceholder(text);
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		updatePalette();
	}, lifetime());
	updatePalette();

	if (_st.textBg->c.alphaF() >= 1. && !_st.borderRadius) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	connect(this, SIGNAL(textChanged(QString)), this, SLOT(onTextChange(QString)));
	connect(this, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onCursorPositionChanged(int,int)));

	connect(this, SIGNAL(textEdited(QString)), this, SLOT(onTextEdited()));
	connect(this, &MaskedInputField::selectionChanged, [] {
		Integration::Instance().textActionsUpdated();
	});

	setStyle(InputStyle::instance());
	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(_textMargins + QMargins(-2, -1, -2, -1));
	setFrame(false);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	setTextMargins(_st.textMargins);

	startPlaceholderAnimation();
	startBorderAnimation();
	finishAnimating();
}

void MaskedInputField::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Text, _st.textFg->c);
	p.setColor(QPalette::Highlight, st::msgInBgSelected->c);
	p.setColor(QPalette::HighlightedText, st::historyTextInFgSelected->c);
	setPalette(p);
}

void MaskedInputField::setCorrectedText(QString &now, int &nowCursor, const QString &newText, int newPos) {
	if (newPos < 0 || newPos > newText.size()) {
		newPos = newText.size();
	}
	auto updateText = (newText != now);
	if (updateText) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	auto updateCursorPosition = (newPos != nowCursor) || updateText;
	if (updateCursorPosition) {
		nowCursor = newPos;
		setCursorPosition(nowCursor);
	}
}

void MaskedInputField::customUpDown(bool custom) {
	_customUpDown = custom;
}

int MaskedInputField::borderAnimationStart() const {
	return _borderAnimationStart;
}

void MaskedInputField::setTextMargins(const QMargins &mrg) {
	_textMargins = mrg;
	setContentsMargins(_textMargins + QMargins(-2, -1, -2, -1));
	refreshPlaceholder(_placeholderFull.current());
}

void MaskedInputField::onTouchTimer() {
	_touchRightButton = true;
}

bool MaskedInputField::eventHook(QEvent *e) {
	auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		auto event = static_cast<QTouchEvent*>(e);
		if (event->device()->type() == base::TouchDevice::TouchScreen) {
			touchEvent(event);
		}
	}
	return Parent::eventHook(e);
}

void MaskedInputField::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) {
			return;
		}
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = _mousePressedInTouch = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!e->touchPoints().isEmpty()) {
			touchUpdate(e->touchPoints().cbegin()->screenPos().toPoint());
		}
	} break;

	case QEvent::TouchEnd: {
		touchFinish();
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.stop();
	} break;
	}
}

void MaskedInputField::touchUpdate(QPoint globalPosition) {
	if (_touchPress
		&& !_touchMove
		&& ((globalPosition - _touchStart).manhattanLength()
			>= QApplication::startDragDistance())) {
		_touchMove = true;
	}
}

void MaskedInputField::touchFinish() {
	if (!_touchPress) {
		return;
	}
	const auto weak = MakeWeak(this);
	if (!_touchMove && window()) {
		QPoint mapped(mapFromGlobal(_touchStart));

		if (_touchRightButton) {
			QContextMenuEvent contextEvent(
				QContextMenuEvent::Mouse,
				mapped,
				_touchStart);
			contextMenuEvent(&contextEvent);
		} else {
			QGuiApplication::inputMethod()->show();
		}
	}
	if (weak) {
		_touchTimer.stop();
		_touchPress
			= _touchMove
			= _touchRightButton
			= _mousePressedInTouch = false;
	}
}

void MaskedInputField::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto r = rect().intersected(e->rect());
	p.fillRect(r, _st.textBg);
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg->b);
	}
	auto errorDegree = _a_error.value(_error ? 1. : 0.);
	auto focusedDegree = _a_focused.value(_focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.value(1.);
	auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = std::clamp(_borderAnimationStart, 0, width());
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width() - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height() - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}

	p.setClipRect(r);
	if (_st.placeholderScale > 0. && !_placeholderPath.isEmpty()) {
		auto placeholderShiftDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		p.save();
		p.setClipRect(r);

		auto placeholderTop = anim::interpolate(0, _st.placeholderShift, placeholderShiftDegree);

		QRect r(rect().marginsRemoved(_textMargins + _st.placeholderMargins));
		r.moveTop(r.top() + placeholderTop);
		if (style::RightToLeft()) r.moveLeft(width() - r.left() - r.width());

		auto placeholderScale = 1. - (1. - _st.placeholderScale) * placeholderShiftDegree;
		auto placeholderFg = anim::color(_st.placeholderFg, _st.placeholderFgActive, focusedDegree);
		placeholderFg = anim::color(placeholderFg, _st.placeholderFgError, errorDegree);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(placeholderFg);
		p.translate(r.topLeft());
		p.scale(placeholderScale, placeholderScale);
		p.drawPath(_placeholderPath);

		p.restore();
	} else if (!_placeholder.isEmpty()) {
		auto placeholderHiddenDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			QRect r(rect().marginsRemoved(_textMargins + _st.placeholderMargins));
			r.moveLeft(r.left() + placeholderLeft);
			if (style::RightToLeft()) r.moveLeft(width() - r.left() - r.width());

			p.setFont(_st.placeholderFont);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));
			p.drawText(r, _placeholder, _st.placeholderAlign);

			p.restore();
			p.setOpacity(1.);
		}
	}

	paintAdditionalPlaceholder(p);
	QLineEdit::paintEvent(e);
}

void MaskedInputField::mousePressEvent(QMouseEvent *e) {
	if (_touchPress && e->button() == Qt::LeftButton) {
		_mousePressedInTouch = true;
		_touchStart = e->globalPos();
	}
	return QLineEdit::mousePressEvent(e);
}

void MaskedInputField::mouseReleaseEvent(QMouseEvent *e) {
	if (_mousePressedInTouch) {
		touchFinish();
	}
	return QLineEdit::mouseReleaseEvent(e);
}

void MaskedInputField::mouseMoveEvent(QMouseEvent *e) {
	if (_mousePressedInTouch) {
		touchUpdate(e->globalPos());
	}
	return QLineEdit::mouseMoveEvent(e);
}

QString MaskedInputField::getDisplayedText() const {
	auto result = getLastText();
	if (!_lastPreEditText.isEmpty()) {
		result = result.mid(0, _oldcursor)
			+ _lastPreEditText
			+ result.mid(_oldcursor);
	}
	return result;
}

void MaskedInputField::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([this] { update(); }, 0., 1., _st.duration);
			} else {
				_a_borderShown.start([this] { update(); }, 0., 1., _st.duration);
			}
		} else if (qFuzzyCompare(_a_borderShown.value(1.), 0.)) {
			_a_borderShown.stop();
			_a_borderOpacity.stop();
		} else {
			_a_borderOpacity.start([this] { update(); }, 1., 0., _st.duration);
		}
	}
}

void MaskedInputField::focusInEvent(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason) ? mapFromGlobal(QCursor::pos()).x() : (width() / 2);
	setFocused(true);
	QLineEdit::focusInEvent(e);
	focused();
}

void MaskedInputField::focusOutEvent(QFocusEvent *e) {
	setFocused(false);
	QLineEdit::focusOutEvent(e);
	blurred();
}

void MaskedInputField::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start([this] { update(); }, _focused ? 0. : 1., _focused ? 1. : 0., _st.duration);
		startPlaceholderAnimation();
		startBorderAnimation();
	}
}

void MaskedInputField::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder(_placeholderFull.current());
	_borderAnimationStart = width() / 2;
	QLineEdit::resizeEvent(e);
}

void MaskedInputField::refreshPlaceholder(const QString &text) {
	const auto availableWidth = width() - _textMargins.left() - _textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		const auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(text, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			const auto result = style::FindAdjustResult(placeholderFont);
			const auto ascent = result ? result->iascent : metrics.ascent();
			_placeholderPath.addText(0, ascent, placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(text, availableWidth);
	}
	update();
}

void MaskedInputField::setPlaceholder(rpl::producer<QString> placeholder) {
	_placeholderFull = std::move(placeholder);
}

void MaskedInputField::contextMenuEvent(QContextMenuEvent *e) {
	if (const auto menu = createStandardContextMenu()) {
		_contextMenu = base::make_unique_q<PopupMenu>(this, menu);
		_contextMenu->popup(e->globalPos());
	}
}

void MaskedInputField::inputMethodEvent(QInputMethodEvent *e) {
	QLineEdit::inputMethodEvent(e);
	_lastPreEditText = e->preeditString();
	update();
}

void MaskedInputField::showError() {
	showErrorNoFocus();
	if (!hasFocus()) {
		setFocus();
	}
}

void MaskedInputField::showErrorNoFocus() {
	setErrorShown(true);
}

void MaskedInputField::hideError() {
	setErrorShown(false);
}

void MaskedInputField::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start([this] { update(); }, _error ? 0. : 1., _error ? 1. : 0., _st.duration);
		startBorderAnimation();
	}
}

QSize MaskedInputField::sizeHint() const {
	return geometry().size();
}

QSize MaskedInputField::minimumSizeHint() const {
	return geometry().size();
}

void MaskedInputField::setDisplayFocused(bool focused) {
	setFocused(focused);
	finishAnimating();
}

void MaskedInputField::finishAnimating() {
	_a_focused.stop();
	_a_error.stop();
	_a_placeholderShifted.stop();
	_a_borderShown.stop();
	_a_borderOpacity.stop();
	update();
}

void MaskedInputField::setPlaceholderHidden(bool forcePlaceholderHidden) {
	_forcePlaceholderHidden = forcePlaceholderHidden;
	startPlaceholderAnimation();
}

void MaskedInputField::startPlaceholderAnimation() {
	auto placeholderShifted = _forcePlaceholderHidden || (_focused && _st.placeholderScale > 0.) || !getLastText().isEmpty();
	if (_placeholderShifted != placeholderShifted) {
		_placeholderShifted = placeholderShifted;
		_a_placeholderShifted.start([this] { update(); }, _placeholderShifted ? 0. : 1., _placeholderShifted ? 1. : 0., _st.duration);
	}
}

QRect MaskedInputField::placeholderRect() const {
	return rect().marginsRemoved(_textMargins + _st.placeholderMargins);
}

style::font MaskedInputField::phFont() {
	return _st.style.font;
}

void MaskedInputField::placeholderAdditionalPrepare(QPainter &p) {
	p.setFont(_st.style.font);
	p.setPen(_st.placeholderFg);
}

void MaskedInputField::keyPressEvent(QKeyEvent *e) {
	QString wasText(_oldtext);
	int32 wasCursor(_oldcursor);

	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
		e->ignore();
	} else if (e == QKeySequence::DeleteStartOfWord && hasSelectedText()) {
		e->accept();
		backspace();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	auto newText = text();
	auto newCursor = cursorPosition();
	if (wasText == newText && wasCursor == newCursor) { // call correct manually
		correctValue(wasText, wasCursor, newText, newCursor);
		_oldtext = newText;
		_oldcursor = newCursor;
		if (wasText != _oldtext) changed();
		startPlaceholderAnimation();
	}
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
		cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		submitted(e->modifiers());
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto selected = selectedText();
		if (!selected.isEmpty() && echoMode() == QLineEdit::Normal) {
			QGuiApplication::clipboard()->setText(selected, QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	}
}

void MaskedInputField::onTextEdited() {
	QString wasText(_oldtext), newText(text());
	int32 wasCursor(_oldcursor), newCursor(cursorPosition());

	correctValue(wasText, wasCursor, newText, newCursor);
	_oldtext = newText;
	_oldcursor = newCursor;
	if (wasText != _oldtext) changed();
	startPlaceholderAnimation();

	Integration::Instance().textActionsUpdated();
}

void MaskedInputField::onTextChange(const QString &text) {
	_oldtext = QLineEdit::text();
	setErrorShown(false);
	Integration::Instance().textActionsUpdated();
}

void MaskedInputField::onCursorPositionChanged(int oldPosition, int position) {
	_oldcursor = position;
}

} // namespace Ui
