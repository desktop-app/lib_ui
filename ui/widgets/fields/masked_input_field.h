// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

#include <QtWidgets/QLineEdit>
#include <QtCore/QTimer>

namespace style {
struct InputField;
} // namespace style

namespace Ui {

class PopupMenu;

class MaskedInputField : public RpWidgetBase<QLineEdit> {
	Q_OBJECT

	using Parent = RpWidgetBase<QLineEdit>;
public:
	MaskedInputField(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder = nullptr,
		const QString &val = QString());

	void showError();
	void showErrorNoFocus();
	void hideError();

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	int borderAnimationStart() const;

	const QString &getLastText() const {
		return _oldtext;
	}
	void setPlaceholder(rpl::producer<QString> placeholder);
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void setDisplayFocused(bool focused);
	void finishAnimating();
	void setFocusFast() {
		setDisplayFocused(true);
		setFocus();
	}

	void setText(const QString &text) {
		QLineEdit::setText(text);
		startPlaceholderAnimation();
	}
	void clear() {
		QLineEdit::clear();
		startPlaceholderAnimation();
	}

public Q_SLOTS:
	void onTextChange(const QString &text);
	void onCursorPositionChanged(int oldPosition, int position);

	void onTextEdited();

	void onTouchTimer();

Q_SIGNALS:
	void changed();
	void cancelled();
	void submitted(Qt::KeyboardModifiers);
	void focused();
	void blurred();

protected:
	QString getDisplayedText() const;
	void startBorderAnimation();
	void startPlaceholderAnimation();

	bool eventHook(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

	virtual void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	}
	void setCorrectedText(
		QString &now,
		int &nowCursor,
		const QString &newText,
		int newPos);

	virtual void paintAdditionalPlaceholder(QPainter &p) {
	}

	style::font phFont();

	void placeholderAdditionalPrepare(QPainter &p);
	QRect placeholderRect() const;

	void setTextMargins(const QMargins &mrg);
	const style::InputField &_st;

private:
	void updatePalette();
	void refreshPlaceholder(const QString &text);
	void setErrorShown(bool error);

	void touchUpdate(QPoint globalPosition);
	void touchFinish();

	void setFocused(bool focused);

	int _maxLength = -1;
	bool _forcePlaceholderHidden = false;

	QString _oldtext;
	int _oldcursor = 0;
	QString _lastPreEditText;

	bool _undoAvailable = false;
	bool _redoAvailable = false;

	bool _customUpDown = false;

	rpl::variable<QString> _placeholderFull;
	QString _placeholder;
	Animations::Simple _a_placeholderShifted;
	bool _placeholderShifted = false;
	QPainterPath _placeholderPath;

	Animations::Simple _a_borderShown;
	int _borderAnimationStart = 0;
	Animations::Simple _a_borderOpacity;
	bool _borderVisible = false;

	Animations::Simple _a_focused;
	Animations::Simple _a_error;

	bool _focused = false;
	bool _error = false;

	style::margins _textMargins;

	QTimer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	bool _mousePressedInTouch = false;
	QPoint _touchStart;

	base::unique_qptr<PopupMenu> _contextMenu;

};

} // namespace Ui
