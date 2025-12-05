// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/abstract_button.h"
#include "ui/round_rect.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "styles/style_widgets.h"

#include <cstddef>
#include <memory>

class Painter;

namespace st {
extern const style::SettingsButton &defaultSettingsButton;
} // namespace st

namespace Ui {

class RippleAnimation;
class NumbersAnimation;
class ToggleView;

class LinkButton : public AbstractButton {
public:
	LinkButton(QWidget *parent, const QString &text, const style::LinkButton &st = st::defaultLinkButton);

	void setText(const QString &text);
	void setColorOverride(std::optional<QColor> textFg);

	QAccessible::Role accessibilityRole() override {
		return QAccessible::Role::Link;
	}
	QString accessibilityName() override {
		return _text;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	void resizeToText();

	int resizeGetHeight(int newWidth) override;

	const style::LinkButton &_st;
	QString _text;
	int _textWidth = 0;
	std::optional<QColor> _textFgOverride;

};

class RippleButton : public AbstractButton {
public:
	RippleButton(QWidget *parent, const style::RippleAnimation &st);

	void setForceRippled(
		bool rippled,
		anim::type animated = anim::type::normal);
	bool forceRippled() const {
		return _forceRippled;
	}

	static QPoint DisabledRippleStartPosition() {
		return QPoint(-0x3FFFFFFF, -0x3FFFFFFF);
	}

	void clearState() override;

	void paintRipple(
		QPainter &p,
		const QPoint &point,
		const QColor *colorOverride = nullptr);
	void paintRipple(
		QPainter &p,
		int x,
		int y,
		const QColor *colorOverride = nullptr);

	void finishAnimating();

	~RippleButton();

protected:
	void onStateChanged(State was, StateChangeSource source) override;

	virtual QImage prepareRippleMask() const;
	virtual QPoint prepareRippleStartPosition() const;

private:
	void ensureRipple();

	const style::RippleAnimation &_st;
	std::unique_ptr<RippleAnimation> _ripple;
	bool _forceRippled = false;
	rpl::lifetime _forceRippledSubscription;

};

class FlatButton : public RippleButton {
public:
	FlatButton(QWidget *parent, const QString &text, const style::FlatButton &st);

	QString accessibilityName() override {
		return _text;
	}

	void setText(const QString &text);
	void setWidth(int w);
	void setColorOverride(std::optional<QColor> color);
	void setTextMargins(QMargins margins);

	int32 textWidth() const;

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	QString _text;
	QMargins _textMargins;
	int _width = 0;
	std::optional<QColor> _colorOverride;

	const style::FlatButton &_st;

};

class RoundButton : public RippleButton {
public:
	RoundButton(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::RoundButton &st);

	QString accessibilityName() override {
		return _textFull.current().text;
	}

	[[nodiscard]] const style::RoundButton &st() const {
		return _st;
	}

	void setText(rpl::producer<QString> text);
	void setText(rpl::producer<TextWithEntities> text);
	void setContext(const Text::MarkedContext &context);

	void setNumbersText(const QString &numbersText) {
		setNumbersText(numbersText, numbersText.toInt());
	}
	void setNumbersText(int numbers) {
		setNumbersText(QString::number(numbers), numbers);
	}
	void setWidthChangedCallback(Fn<void()> callback);
	void setBrushOverride(std::optional<QBrush> brush);
	void setPenOverride(std::optional<QPen> pen);
	void setTextFgOverride(std::optional<QColor> textFg);
	void setIconOverride(const style::icon *icon);
	void finishNumbersAnimation();

	[[nodiscard]] int contentWidth() const;

	void setFullWidth(int newFullWidth);
	void setFullRadius(bool enabled);

	enum class TextTransform {
		NoTransform,
		ToUpper,
	};
	void setTextTransform(TextTransform transform);

	~RoundButton();

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void setNumbersText(const QString &numbersText, int numbers);
	void numbersAnimationCallback();
	void resizeToText(const TextWithEntities &text);
	[[nodiscard]] int addedWidth() const;

	rpl::variable<TextWithEntities> _textFull;
	Ui::Text::String _text;

	std::unique_ptr<NumbersAnimation> _numbers;

	int _fullWidthOverride = 0;

	const style::RoundButton &_st;
	std::optional<QBrush> _brushOverride;
	std::optional<QPen> _penOverride;
	std::optional<QColor> _textFgOverride;
	const style::icon *_iconOverride = nullptr;
	RoundRect _roundRect;
	RoundRect _roundRectOver;
	Text::MarkedContext _context;

	TextTransform _transform = TextTransform::ToUpper;
	bool _fullRadius = false;

};

class IconButton : public RippleButton {
public:
	IconButton(QWidget *parent, const style::IconButton &st);

	[[nodiscard]] const style::IconButton &st() const;

	// Pass nullptr to restore the default icon.
	void setIconOverride(const style::icon *iconOverride, const style::icon *iconOverOverride = nullptr);
	void setRippleColorOverride(const style::color *colorOverride);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	[[nodiscard]] float64 iconOverOpacity() const;

private:
	const style::IconButton &_st;
	const style::icon *_iconOverride = nullptr;
	const style::icon *_iconOverrideOver = nullptr;
	const style::color *_rippleColorOverride = nullptr;

	Ui::Animations::Simple _a_over;

};

class CrossButton : public RippleButton {
public:
	CrossButton(QWidget *parent, const style::CrossButton &st);

	void toggle(bool shown, anim::type animated);
	void show(anim::type animated) {
		return toggle(true, animated);
	}
	void hide(anim::type animated) {
		return toggle(false, animated);
	}
	void finishAnimating() {
		_showAnimation.stop();
		animationCallback();
	}

	bool toggled() const {
		return _shown;
	}
	void setLoadingAnimation(bool enabled);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	bool loadingCallback(crl::time now);
	bool stopLoadingAnimation(crl::time now);
	void animationCallback();

	const style::CrossButton &_st;

	bool _shown = false;
	Ui::Animations::Simple _showAnimation;

	crl::time _loadingStopMs = 0;
	Ui::Animations::Basic _loadingAnimation;

};

class SettingsButton : public Ui::RippleButton {
public:
	SettingsButton(
		QWidget *parent,
		rpl::producer<QString> &&text,
		const style::SettingsButton &st = st::defaultSettingsButton);
	SettingsButton(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&text,
		const style::SettingsButton &st = st::defaultSettingsButton,
		const Text::MarkedContext &context = {});
	SettingsButton(
		QWidget *parent,
		std::nullptr_t,
		const style::SettingsButton &st = st::defaultSettingsButton);
	~SettingsButton();

	QString accessibilityName() override {
		return _text.toString();
	}

	SettingsButton *toggleOn(
		rpl::producer<bool> &&toggled,
		bool ignoreClick = false);
	bool toggled() const;
	rpl::producer<bool> toggledChanges() const;
	rpl::producer<bool> toggledValue() const;

	void setToggleLocked(bool locked);
	void setColorOverride(std::optional<QColor> textColorOverride);
	void setPaddingOverride(style::margins padding);

	[[nodiscard]] const style::SettingsButton &st() const;
	[[nodiscard]] int fullTextWidth() const;

	void finishAnimating();

protected:
	int resizeGetHeight(int newWidth) override;
	void onStateChanged(
		State was,
		StateChangeSource source) override;

	void paintEvent(QPaintEvent *e) override;

	void paintBg(Painter &p, const QRect &rect, bool over) const;
	void paintText(Painter &p, bool over, int outerw) const;
	void paintToggle(Painter &p, int outerw) const;

	[[nodiscard]] QRect maybeToggleRect() const;

private:
	void setText(TextWithEntities &&text);
	[[nodiscard]] QRect toggleRect() const;

	const style::SettingsButton &_st;
	style::margins _padding;
	Ui::Text::String _text;
	std::unique_ptr<Ui::ToggleView> _toggle;
	std::optional<QColor> _textColorOverride;
	Text::MarkedContext _context;

};

[[nodiscard]] not_null<RippleButton*> CreateSimpleRectButton(
	QWidget *parent,
	const style::RippleAnimation &st);
[[nodiscard]] not_null<RippleButton*> CreateSimpleSettingsButton(
	QWidget *parent,
	const style::RippleAnimation &st,
	const style::color &bg);
[[nodiscard]] not_null<RippleButton*> CreateSimpleCircleButton(
	QWidget *parent,
	const style::RippleAnimation &st);
[[nodiscard]] not_null<RippleButton*> CreateSimpleRoundButton(
	QWidget *parent,
	const style::RippleAnimation &st);

} // namespace Ui
