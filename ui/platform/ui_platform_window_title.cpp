// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/ui_platform_window_title.h"

#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"
#include "ui/widgets/rp_window.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"
#include "base/algorithm.h"
#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>

namespace Ui {
namespace Platform {
namespace {

template <typename T>
void RemoveDuplicates(std::vector<T> &v) {
	auto end = v.end();
	for (auto it = v.begin(); it != end; ++it) {
		end = std::remove(it + 1, end, *it);
	}

	v.erase(end, v.end());
}

} // namespace

bool SemiNativeSystemButtonProcessing() {
	return ::Platform::IsWindows11OrGreater();
}

void SetupSemiNativeSystemButtons(
		not_null<TitleControls*> controls,
		not_null<RpWindow*> window,
		rpl::lifetime &lifetime,
		Fn<bool()> filter) {
	if (!SemiNativeSystemButtonProcessing()) {
		return;
	}

	window->systemButtonOver(
	) | rpl::filter([=](HitTestResult button) {
		return !filter || filter() || (button == HitTestResult::None);
	}) | rpl::start_with_next([=](HitTestResult button) {
		controls->buttonOver(button);
	}, lifetime);

	window->systemButtonDown(
	) | rpl::filter([=](HitTestResult button) {
		return !filter || filter() || (button == HitTestResult::None);
	}) | rpl::start_with_next([=](HitTestResult button) {
		controls->buttonDown(button);
	}, lifetime);
}

class TitleControls::Button final : public IconButton {
public:
	using IconButton::IconButton;

	void setOver(bool over) {
		IconButton::setOver(over, StateChangeSource::ByPress);
	}
	void setDown(bool down) {
		IconButton::setDown(
			down,
			StateChangeSource::ByPress,
			{},
			Qt::LeftButton);
	}
};

TitleControls::TitleControls(
	not_null<RpWidget*> parent,
	const style::WindowTitle &st,
	Fn<void(bool maximized)> maximize)
: _st(&st)
, _minimize(parent, _st->minimize)
, _maximizeRestore(parent, _st->maximize)
, _close(parent, _st->close)
, _maximizedState(parent->windowState()
	& (Qt::WindowMaximized | Qt::WindowFullScreen))
, _activeState(parent->isActiveWindow()) {
	init(std::move(maximize));

	_close->paintRequest(
	) | rpl::start_with_next([=] {
		const auto active = window()->isActiveWindow();
		if (_activeState != active) {
			_activeState = active;
			updateButtonsState();
		}
	}, _close->lifetime());
}

void TitleControls::setStyle(const style::WindowTitle &st) {
	_st = &st;
	updateButtonsState();
}

not_null<const style::WindowTitle*> TitleControls::st() const {
	return _st;
}

QRect TitleControls::geometry() const {
	auto result = QRect();
	const auto add = [&](auto &&control) {
		if (!control->isHidden()) {
			result = result.united(control->geometry());
		}
	};
	add(_minimize);
	add(_maximizeRestore);
	add(_close);
	return result;
}

not_null<RpWidget*> TitleControls::parent() const {
	return static_cast<RpWidget*>(_close->parentWidget());
}

not_null<QWidget*> TitleControls::window() const {
	return _close->window();
}

void TitleControls::init(Fn<void(bool maximized)> maximize) {
	_minimize->setClickedCallback([=] {
		const auto weak = MakeWeak(_minimize.data());
		window()->setWindowState(
			window()->windowState() | Qt::WindowMinimized);
		if (weak) {
			_minimize->clearState();
		}
	});
	_minimize->setPointerCursor(false);
	_maximizeRestore->setClickedCallback([=] {
		const auto weak = MakeWeak(_maximizeRestore.data());
		if (maximize) {
			maximize(!_maximizedState);
		} else {
			window()->setWindowState(_maximizedState
				? Qt::WindowNoState
				: Qt::WindowMaximized);
		}
		if (weak) {
			_maximizeRestore->clearState();
		}
	});
	_maximizeRestore->setPointerCursor(false);
	_close->setClickedCallback([=] {
		const auto weak = MakeWeak(_close.data());
		window()->close();
		if (weak) {
			_close->clearState();
		}
	});
	_close->setPointerCursor(false);

	parent()->widthValue(
	) | rpl::start_with_next([=](int width) {
		updateControlsPosition();
	}, _close->lifetime());

	TitleControlsLayoutChanged(
	) | rpl::start_with_next([=] {
		updateControlsPosition();
	}, _close->lifetime());

	subscribeToStateChanges();

	_activeState = parent()->isActiveWindow();
	updateButtonsState();
}

void TitleControls::subscribeToStateChanges() {
	const auto subscribe = [=] {
		QObject::connect(
			window()->windowHandle(),
			&QWindow::windowStateChanged,
			[=](Qt::WindowState state) { handleWindowStateChanged(state); });
	};
	if (window()->windowHandle()) {
		subscribe();
	} else {
		const auto winIdEventFilter = std::make_shared<QObject*>(nullptr);
		*winIdEventFilter = base::install_event_filter(
			window(),
			[=](not_null<QEvent*> e) {
				if (!*winIdEventFilter || e->type() != QEvent::WinIdChange) {
					return base::EventFilterResult::Continue;
				}
				subscribe();
				base::take(*winIdEventFilter)->deleteLater();
				return base::EventFilterResult::Continue;
			});
	}
}

void TitleControls::setResizeEnabled(bool enabled) {
	_resizeEnabled = enabled;
	updateControlsPosition();
}

void TitleControls::raise() {
	_minimize->raise();
	_maximizeRestore->raise();
	_close->raise();
}

HitTestResult TitleControls::hitTest(QPoint point) const {
	const auto test = [&](const object_ptr<Button> &button) {
		return button && button->geometry().contains(point);
	};
	if (test(_minimize)) {
		return HitTestResult::Minimize;
	} else if (test(_maximizeRestore)) {
		return HitTestResult::MaximizeRestore;
	} else if (test(_close)) {
		return HitTestResult::Close;
	}
	return HitTestResult::None;
}

void TitleControls::buttonOver(HitTestResult testResult) {
	const auto update = [&](
			const object_ptr<Button> &button,
			HitTestResult buttonTestResult) {
		if (const auto raw = button.data()) {
			raw->setOver(testResult == buttonTestResult);
		}
	};
	update(_minimize, HitTestResult::Minimize);
	update(_maximizeRestore, HitTestResult::MaximizeRestore);
	update(_close, HitTestResult::Close);
}

void TitleControls::buttonDown(HitTestResult testResult) {
	const auto update = [&](
			const object_ptr<Button> &button,
			HitTestResult buttonTestResult) {
		if (const auto raw = button.data()) {
			raw->setDown(testResult == buttonTestResult);
		}
	};
	update(_minimize, HitTestResult::Minimize);
	update(_maximizeRestore, HitTestResult::MaximizeRestore);
	update(_close, HitTestResult::Close);
}

TitleControls::Button *TitleControls::controlWidget(Control control) const {
	switch (control) {
	case Control::Minimize: return _minimize;
	case Control::Maximize: return _maximizeRestore;
	case Control::Close: return _close;
	}

	return nullptr;
}

void TitleControls::updateControlsPosition() {
	const auto controlsLayout = TitleControlsLayout();
	auto controlsLeft = controlsLayout.left;
	auto controlsRight = controlsLayout.right;
	const auto moveFromTo = [&](auto &from, auto &to) {
		for (const auto control : from) {
			if (!ranges::contains(to, control)) {
				to.push_back(control);
			}
		}
		from.clear();
	};
	if (ranges::contains(controlsLeft, Control::Close)) {
		moveFromTo(controlsRight, controlsLeft);
	} else if (ranges::contains(controlsRight, Control::Close)) {
		moveFromTo(controlsLeft, controlsRight);
	} else if (controlsLeft.size() > controlsRight.size()) {
		moveFromTo(controlsRight, controlsLeft);
	} else {
		moveFromTo(controlsLeft, controlsRight);
	}

	const auto controlPresent = [&](Control control) {
		return ranges::contains(controlsLeft, control)
		|| ranges::contains(controlsRight, control);
	};

	const auto eraseControl = [&](Control control) {
		controlsLeft.erase(
			ranges::remove(controlsLeft, control),
			end(controlsLeft));

		controlsRight.erase(
			ranges::remove(controlsRight, control),
			end(controlsRight));
	};

	if (!_resizeEnabled) {
		eraseControl(Control::Maximize);
	}

	if (controlPresent(Control::Minimize)) {
		_minimize->show();
	} else {
		_minimize->hide();
	}

	if (controlPresent(Control::Maximize)) {
		_maximizeRestore->show();
	} else {
		_maximizeRestore->hide();
	}

	if (controlPresent(Control::Close)) {
		_close->show();
	} else {
		_close->hide();
	}

	updateControlsPositionBySide(controlsLeft, false);
	updateControlsPositionBySide(controlsRight, true);
}

void TitleControls::updateControlsPositionBySide(
		const std::vector<Control> &controls,
		bool right) {
	auto preparedControls = right
		? (ranges::views::reverse(controls) | ranges::to_vector)
		: controls;

	RemoveDuplicates(preparedControls);

	auto position = 0;
	for (const auto &control : preparedControls) {
		const auto widget = controlWidget(control);
		if (!widget) {
			continue;
		}

		if (right) {
			widget->moveToRight(position, 0);
		} else {
			widget->moveToLeft(position, 0);
		}

		position += widget->width();
	}
}

void TitleControls::handleWindowStateChanged(Qt::WindowState state) {
	if (state == Qt::WindowMinimized) {
		return;
	}

	auto maximized = (state == Qt::WindowMaximized)
		|| (state == Qt::WindowFullScreen);
	if (_maximizedState != maximized) {
		_maximizedState = maximized;
		updateButtonsState();
	}
}

void TitleControls::updateButtonsState() {
	const auto minimize = _activeState
		? &_st->minimizeIconActive
		: &_st->minimize.icon;
	const auto minimizeOver = _activeState
		? &_st->minimizeIconActiveOver
		: &_st->minimize.iconOver;
	_minimize->setIconOverride(minimize, minimizeOver);
	if (_maximizedState) {
		const auto restore = _activeState
			? &_st->restoreIconActive
			: &_st->restoreIcon;
		const auto restoreOver = _activeState
			? &_st->restoreIconActiveOver
			: &_st->restoreIconOver;
		_maximizeRestore->setIconOverride(restore, restoreOver);
	} else {
		const auto maximize = _activeState
			? &_st->maximizeIconActive
			: &_st->maximize.icon;
		const auto maximizeOver = _activeState
			? &_st->maximizeIconActiveOver
			: &_st->maximize.iconOver;
		_maximizeRestore->setIconOverride(maximize, maximizeOver);
	}
	const auto close = _activeState
		? &_st->closeIconActive
		: &_st->close.icon;
	const auto closeOver = _activeState
		? &_st->closeIconActiveOver
		: &_st->close.iconOver;
	_close->setIconOverride(close, closeOver);
}

DefaultTitleWidget::DefaultTitleWidget(not_null<RpWidget*> parent)
: RpWidget(parent)
, _controls(this, st::defaultWindowTitle)
, _shadow(this, st::titleShadow) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

not_null<const style::WindowTitle*> DefaultTitleWidget::st() const {
	return _controls.st();
}

void DefaultTitleWidget::setText(const QString &text) {
	window()->setWindowTitle(text);
}

void DefaultTitleWidget::setStyle(const style::WindowTitle &st) {
	_controls.setStyle(st);
	update();
}

void DefaultTitleWidget::setResizeEnabled(bool enabled) {
	_controls.setResizeEnabled(enabled);
}

void DefaultTitleWidget::paintEvent(QPaintEvent *e) {
	const auto active = window()->isActiveWindow();
	QPainter(this).fillRect(
		e->rect(),
		active ? _controls.st()->bgActive : _controls.st()->bg);
}

void DefaultTitleWidget::resizeEvent(QResizeEvent *e) {
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void DefaultTitleWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_mousePressed = true;
	} else if (e->button() == Qt::RightButton) {
		ShowWindowMenu(window()->windowHandle());
	}
}

void DefaultTitleWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_mousePressed = false;
	}
}

void DefaultTitleWidget::mouseMoveEvent(QMouseEvent *e) {
	if (_mousePressed) {
		window()->windowHandle()->startSystemMove();
	}
}

void DefaultTitleWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	const auto state = window()->windowState();
	if (state & Qt::WindowMaximized) {
		window()->setWindowState(state & ~Qt::WindowMaximized);
	} else {
		window()->setWindowState(state | Qt::WindowMaximized);
	}
}

SeparateTitleControls::SeparateTitleControls(
	QWidget *parent,
	const style::WindowTitle &st,
	Fn<void(bool maximized)> maximize)
: wrap(parent)
, controls(&wrap, st, std::move(maximize)) {
}

std::unique_ptr<SeparateTitleControls> SetupSeparateTitleControls(
		not_null<RpWindow*> window,
		const style::WindowTitle &st,
		Fn<void(bool maximized)> maximize) {
	auto result = std::make_unique<SeparateTitleControls>(
		window->body(),
		st,
		std::move(maximize));

	const auto raw = result.get();
	auto &lifetime = raw->wrap.lifetime();
	rpl::combine(
		window->body()->widthValue(),
		window->additionalContentPaddingValue()
	) | rpl::start_with_next([=](int width, int padding) {
		raw->wrap.setGeometry(
			padding,
			0,
			width - 2 * padding,
			raw->controls.geometry().height());
	}, lifetime);

	window->hitTestRequests(
	) | rpl::start_with_next([=](not_null<HitTestRequest*> request) {
		const auto origin = raw->wrap.pos();
		const auto relative = request->point - origin;
		const auto controlsResult = raw->controls.hitTest(relative);
		if (controlsResult != HitTestResult::None) {
			request->result = controlsResult;
		}
	}, lifetime);

	SetupSemiNativeSystemButtons(&raw->controls, window, lifetime);

	return result;
}

} // namespace Platform
} // namespace Ui
