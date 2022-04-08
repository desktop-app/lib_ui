// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "styles/style_widgets.h"

class Painter;

namespace Ui {

class AbstractCheckView {
public:
	AbstractCheckView(int duration, bool checked, Fn<void()> updateCallback);

	void setChecked(bool checked, anim::type animated);
	void finishAnimating();
	void setUpdateCallback(Fn<void()> updateCallback);
	bool checked() const {
		return _checked;
	}
	void update();
	float64 currentAnimationValue();
	bool animating() const;

	auto checkedChanges() const {
		return _checks.events();
	}
	auto checkedValue() const {
		return _checks.events_starting_with(checked());
	}

	virtual QSize getSize() const = 0;

	virtual void paint(Painter &p, int left, int top, int outerWidth) = 0;
	virtual QImage prepareRippleMask() const = 0;
	virtual bool checkRippleStartPosition(QPoint position) const = 0;

	virtual ~AbstractCheckView() = default;

private:
	virtual void checkedChangedHook(anim::type animated) {
	}

	int _duration = 0;
	bool _checked = false;
	Fn<void()> _updateCallback;
	Ui::Animations::Simple _toggleAnimation;

	rpl::event_stream<bool> _checks;

};

class CheckView : public AbstractCheckView {
public:
	CheckView(
		const style::Check &st,
		bool checked,
		Fn<void()> updateCallback = nullptr);

	void setStyle(const style::Check &st);

	QSize getSize() const override;
	void paint(Painter &p, int left, int top, int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

	void setUntoggledOverride(
		std::optional<QColor> untoggledOverride);

private:
	QSize rippleSize() const;

	not_null<const style::Check*> _st;
	std::optional<QColor> _untoggledOverride;

};

class RadioView : public AbstractCheckView {
public:
	RadioView(
		const style::Radio &st,
		bool checked,
		Fn<void()> updateCallback = nullptr);

	void setStyle(const style::Radio &st);

	void setToggledOverride(std::optional<QColor> toggledOverride);
	void setUntoggledOverride(std::optional<QColor> untoggledOverride);

	QSize getSize() const override;
	void paint(Painter &p, int left, int top, int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;

private:
	QSize rippleSize() const;

	not_null<const style::Radio*> _st;
	std::optional<QColor> _toggledOverride;
	std::optional<QColor> _untoggledOverride;

};

class ToggleView : public AbstractCheckView {
public:
	ToggleView(
		const style::Toggle &st,
		bool checked,
		Fn<void()> updateCallback = nullptr);

	void setStyle(const style::Toggle &st);

	QSize getSize() const override;
	void paint(Painter &p, int left, int top, int outerWidth) override;
	QImage prepareRippleMask() const override;
	bool checkRippleStartPosition(QPoint position) const override;
	void setLocked(bool locked);

private:
	void paintXV(Painter &p, int left, int top, int outerWidth, float64 toggled, const QBrush &brush);
	QSize rippleSize() const;

	not_null<const style::Toggle*> _st;
	bool _locked = false;

};

class Checkbox : public RippleButton {
public:
	Checkbox(
		QWidget *parent,
		const QString &text,
		bool checked = false,
		const style::Checkbox &st = st::defaultCheckbox,
		const style::Check &checkSt = st::defaultCheck);
	Checkbox(
		QWidget *parent,
		const TextWithEntities &text,
		bool checked = false,
		const style::Checkbox &st = st::defaultCheckbox,
		const style::Check &checkSt = st::defaultCheck);
	Checkbox(
		QWidget *parent,
		const QString &text,
		bool checked,
		const style::Checkbox &st,
		const style::Toggle &toggleSt);
	Checkbox(
		QWidget *parent,
		rpl::producer<QString> &&text,
		bool checked = false,
		const style::Checkbox &st = st::defaultCheckbox,
		const style::Check &checkSt = st::defaultCheck);
	Checkbox(
		QWidget *parent,
		rpl::producer<QString> &&text,
		bool checked,
		const style::Checkbox &st,
		const style::Toggle &toggleSt);
	Checkbox(
		QWidget *parent,
		const QString &text,
		const style::Checkbox &st,
		std::unique_ptr<AbstractCheckView> check);
	Checkbox(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&text,
		const style::Checkbox &st,
		std::unique_ptr<AbstractCheckView> check);

	void setText(const QString &text, bool rich = false);
	void setCheckAlignment(style::align alignment);
	void setAllowTextLines(int lines = 0);
	void setTextBreakEverywhere(bool allow = true);

	bool checked() const;
	rpl::producer<bool> checkedChanges() const;
	rpl::producer<bool> checkedValue() const;
	enum class NotifyAboutChange {
		Notify,
		DontNotify,
	};
	void setChecked(
		bool checked,
		NotifyAboutChange notify = NotifyAboutChange::Notify);

	void finishAnimating();

	QMargins getMargins() const override {
		return _st.margin;
	}
	int naturalWidth() const override;

	void updateCheck() {
		rtlupdate(checkRect());
	}
	QRect checkRect() const;

	not_null<AbstractCheckView*> checkView() const {
		return _check.get();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;
	int resizeGetHeight(int newWidth) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	virtual void handlePress();

private:
	void resizeToText();
	QPixmap grabCheckCache() const;
	int countTextMinWidth() const;

	const style::Checkbox &_st;
	std::unique_ptr<AbstractCheckView> _check;
	rpl::event_stream<bool> _checkedChanges;
	QPixmap _checkCache;

	style::align _checkAlignment = style::al_left;
	Text::String _text;
	int _allowTextLines = 1;
	bool _textBreakEverywhere = false;

};

class Radiobutton;

class RadiobuttonGroup {
public:
	RadiobuttonGroup() = default;
	RadiobuttonGroup(int value) : _value(value), _hasValue(true) {
	}

	void setChangedCallback(Fn<void(int value)> callback) {
		_changedCallback = std::move(callback);
	}

	bool hasValue() const {
		return _hasValue;
	}
	int value() const {
		return _value;
	}
	void setValue(int value);

private:
	friend class Radiobutton;
	void registerButton(Radiobutton *button) {
		if (!base::contains(_buttons, button)) {
			_buttons.push_back(button);
		}
	}
	void unregisterButton(Radiobutton *button) {
		_buttons.erase(std::remove(_buttons.begin(), _buttons.end(), button), _buttons.end());
	}

	int _value = 0;
	bool _hasValue = false;
	Fn<void(int value)> _changedCallback;
	std::vector<Radiobutton*> _buttons;

};

class Radiobutton : public Checkbox {
public:
	Radiobutton(
		QWidget *parent,
		const std::shared_ptr<RadiobuttonGroup> &group,
		int value,
		const QString &text,
		const style::Checkbox &st = st::defaultCheckbox,
		const style::Radio &radioSt = st::defaultRadio);
	Radiobutton(
		QWidget *parent,
		const std::shared_ptr<RadiobuttonGroup> &group,
		int value,
		const QString &text,
		const style::Checkbox &st,
		std::unique_ptr<AbstractCheckView> check);
	~Radiobutton();

protected:
	void handlePress() override;

private:
	// Hide the names from Checkbox.
	bool checked() const;
	void checkedChanges() const;
	void checkedValue() const;
	void setChecked(bool checked, NotifyAboutChange notify);

	Checkbox *checkbox() {
		return this;
	}
	const Checkbox *checkbox() const {
		return this;
	}

	friend class RadiobuttonGroup;
	void handleNewGroupValue(int value);

	std::shared_ptr<RadiobuttonGroup> _group;
	int _value = 0;

};

template <typename Enum>
class Radioenum;

template <typename Enum>
class RadioenumGroup {
public:
	RadioenumGroup() = default;
	RadioenumGroup(Enum value) : _group(static_cast<int>(value)) {
	}

	template <typename Callback>
	void setChangedCallback(Callback &&callback) {
		_group.setChangedCallback([callback](int value) {
			callback(static_cast<Enum>(value));
		});
	}

	bool hasValue() const {
		return _group.hasValue();
	}
	Enum value() const {
		return static_cast<Enum>(_group.value());
	}
	void setValue(Enum value) {
		_group.setValue(static_cast<int>(value));
	}

private:
	template <typename OtherEnum>
	friend class Radioenum;

	RadiobuttonGroup _group;

};

template <typename Enum>
class Radioenum : public Radiobutton {
public:
	Radioenum(
		QWidget *parent,
		const std::shared_ptr<RadioenumGroup<Enum>> &group,
		Enum value,
		const QString &text,
		const style::Checkbox &st = st::defaultCheckbox)
	: Radiobutton(
		parent,
		std::shared_ptr<RadiobuttonGroup>(group, &group->_group),
		static_cast<int>(value),
		text,
		st) {
	}
	Radioenum(
		QWidget *parent,
		const std::shared_ptr<RadioenumGroup<Enum>> &group,
		Enum value,
		const QString &text,
		const style::Checkbox &st,
		std::unique_ptr<AbstractCheckView> check)
		: Radiobutton(
			parent,
			std::shared_ptr<RadiobuttonGroup>(group, &group->_group),
			static_cast<int>(value),
			text,
			st,
			std::move(check)) {
	}

};

} // namespace Ui
