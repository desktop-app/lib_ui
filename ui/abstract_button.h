// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <rpl/event_stream.h>
#include "ui/rp_widget.h"
#include "base/flags.h"

namespace Ui {

class AbstractButton : public RpWidget {
public:
	AbstractButton(QWidget *parent);

	[[nodiscard]] Qt::KeyboardModifiers clickModifiers() const {
		return _modifiers;
	}

	void setDisabled(bool disabled = true);
	virtual void clearState();
	[[nodiscard]] bool isOver() const {
		return _state & StateFlag::Over;
	}
	[[nodiscard]] bool isDown() const {
		return _state & StateFlag::Down;
	}
	[[nodiscard]] bool isDisabled() const {
		return _state & StateFlag::Disabled;
	}

	void setSynteticOver(bool over) {
		setOver(over, StateChangeSource::ByPress);
	}
	void setSynteticDown(bool down, Qt::MouseButton button = Qt::LeftButton) {
		setDown(down, StateChangeSource::ByPress, {}, button);
	}

	void setPointerCursor(bool enablePointerCursor);

	void setAcceptBoth(bool acceptBoth = true);

	void setClickedCallback(Fn<void()> callback) {
		_clickedCallback = std::move(callback);
	}

	rpl::producer<Qt::MouseButton> clicks() const {
		return _clicks.events();
	}
	template <typename Handler>
	void addClickHandler(Handler &&handler) {
		clicks(
		) | rpl::start_with_next(
			std::forward<Handler>(handler),
			lifetime());
	}

	void clicked(Qt::KeyboardModifiers modifiers, Qt::MouseButton button);

protected:
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

protected:
	enum class StateFlag {
		None     = 0,
		Over     = (1 << 0),
		Down     = (1 << 1),
		Disabled = (1 << 2),
	};
	friend constexpr bool is_flag_type(StateFlag) { return true; };
	using State = base::flags<StateFlag>;

	State state() const {
		return _state;
	}

	enum class StateChangeSource {
		ByUser = 0x00,
		ByPress = 0x01,
		ByHover = 0x02,
	};
	void setOver(bool over, StateChangeSource source = StateChangeSource::ByUser);
	bool setDown(
		bool down,
		StateChangeSource source,
		Qt::KeyboardModifiers modifiers,
		Qt::MouseButton button);

	virtual void onStateChanged(State was, StateChangeSource source) {
	}

private:
	void updateCursor();
	void checkIfOver(QPoint localPos);

	State _state = StateFlag::None;

	Qt::KeyboardModifiers _modifiers;
	bool _enablePointerCursor = true;
	bool _pointerCursor = false;
	bool _acceptBoth = false;

	Fn<void()> _clickedCallback;

	rpl::event_stream<Qt::MouseButton> _clicks;

};

} // namespace Ui
