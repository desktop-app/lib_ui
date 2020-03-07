// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/side_bar_menu.h"

#include "ui/effects/ripple_animation.h"

namespace Ui {
namespace {

constexpr auto kMaxLabelLines = 3;

} // namespace

SideBarMenu::SideBarMenu(
	not_null<QWidget*> parent,
	const style::SideBarMenu &st)
: _st(st)
, _outer(parent)
, _scroll(Ui::CreateChild<Ui::ScrollArea>(&_outer))
, _inner(_scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(_scroll))) {
	setup();
}

SideBarMenu::~SideBarMenu() = default;

not_null<const Ui::RpWidget*> SideBarMenu::widget() const {
	return &_outer;
}

void SideBarMenu::setGeometry(QRect geometry) {
	_outer.setGeometry(geometry);
}

void SideBarMenu::setItems(std::vector<Item> items) {
	const auto itemId = [](const MenuItem &item) {
		return item.data.id;
	};
	const auto textWidth = _st.minTextWidth;
	const auto finalize = gsl::finally([&] {
		_inner->resize(
			_inner->width(),
			countContentHeight(_inner->width(), _outer.height()));
		_inner->update();
	});
	if (ranges::equal(items, _items, std::less<>(), &Item::id, itemId)) {
		for (auto &&[was, now] : ranges::view::zip(_items, items)) {
			if (was.data.title != now.title) {
				was.data.title = now.title;
				was.text.setText(_st.style, now.title);
			}
			if (was.data.badge != now.badge) {
				was.data.badge = now.badge;
			}
		}
		_inner->update();
		return;
	}
	const auto selected = _selected;
	if (_selected >= 0) {
		setSelected(-1);
	}
	if (_pressed >= 0) {
		setPressed(-1);
	}

	auto current = base::take(_items);
	_items.reserve(items.size());
	for (const auto &item : items) {
		const auto i = ranges::find(current, item.id, itemId);
		if (i != end(current)) {
			_items.push_back(std::move(*i));
		} else {
			_items.push_back({ item });
			_items.back().text = Ui::Text::String(textWidth);
			_items.back().text.setText(_st.style, item.title);
		}
	}
	if (selected >= 0 && selected < _items.size()) {
		setSelected(selected);
	}
}

void SideBarMenu::setActive(const QString &id, anim::type animated) {
	_activeId = id;
	_inner->update();
}

rpl::producer<QString> SideBarMenu::activateRequests() const {
	return _activateRequests.events();
}

rpl::lifetime &SideBarMenu::lifetime() {
	return _outer.lifetime();
}

void SideBarMenu::setup() {
	_inner->move(0, 0);
	_scroll->move(0, 0);

	_outer.sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_scroll->resize(size);
		_inner->resize(
			size.width(),
			countContentHeight(size.width(), size.height()));
	}, lifetime());

	_inner->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = Painter(_inner);
		paint(p, clip);
	}, lifetime());

	_inner->setMouseTracking(true);
	_inner->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::MouseMove:
			mouseMove(static_cast<QMouseEvent*>(e.get())->pos());
			break;
		case QEvent::MouseButtonPress:
			mousePress(static_cast<QMouseEvent*>(e.get())->button());
			break;
		case QEvent::MouseButtonRelease:
			mouseRelease(static_cast<QMouseEvent*>(e.get())->button());
			break;
		case QEvent::Leave:
			setSelected(-1);
			break;
		}
	}, lifetime());

	_outer.show();
}

void SideBarMenu::mouseMove(QPoint position) {
	auto selected = -1;
	auto y = _st.margins.top();
	for (auto &item : _items) {
		if (position.y() < y) {
			break;
		}
		++selected;
		y += item.height;
	}
	if (selected + 1 == _items.size() && position.y() >= y) {
		selected = -1;
	}
	setSelected(selected);
}

void SideBarMenu::mousePress(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	setPressed(_selected);
}

void SideBarMenu::mouseRelease(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	const auto pressed = _pressed;
	setPressed(-1);
	if (_selected != pressed || pressed < 0) {
		return;
	}
	_activateRequests.fire_copy(_items[pressed].data.id);
}

void SideBarMenu::setSelected(int selected) {
	const auto was = (_selected >= 0);
	_selected = selected;
	const auto now = (_selected >= 0);
	if (was != now) {
		_inner->setCursor(now ? style::cur_pointer : style::cur_default);
	}
}

void SideBarMenu::setPressed(int pressed) {
	if (_pressed == pressed) {
		return;
	} else if (_pressed >= 0 && _items[_pressed].ripple) {
		_items[_pressed].ripple->lastStop();
	}
	_pressed = pressed;
	if (_pressed >= 0) {
		addRipple(_items[_pressed], _inner->mapFromGlobal(QCursor::pos()));
	}
}

void SideBarMenu::addRipple(MenuItem &item, QPoint position) {
	auto &ripple = item.ripple;
	const auto id = item.data.id;
	if (!ripple) {
		ripple = std::make_unique<RippleAnimation>(
			st::defaultRippleAnimation,
			RippleAnimation::rectMask({ _inner->width(), item.height }),
			[=] { repaint(id); });
	}
	const auto local = _inner->mapFromGlobal(QCursor::pos());
	ripple->add(local - QPoint(0, item.top));
}

void SideBarMenu::repaint(const QString &id) {
	if (const auto item = itemById(id)) {
		_inner->update(0, item->top, _inner->width(), item->height);
	}
}

SideBarMenu::MenuItem *SideBarMenu::itemById(const QString &id) {
	const auto i = ranges::find(_items, id, [](const MenuItem &item) {
		return item.data.id;
	});
	return (i != end(_items)) ? &*i : nullptr;
}

void SideBarMenu::paint(Painter &p, QRect clip) const {
	auto y = _st.margins.top();
	const auto fullWidth = _inner->width();
	const auto availableWidth = fullWidth
		- _st.margins.left()
		- _st.margins.right();
	p.fillRect(clip, _st.textBg);
	for (const auto &item : _items) {
		if (y + item.height <= clip.y()) {
			y += item.height;
			continue;
		} else if (y >= clip.y() + clip.height()) {
			break;
		}
		const auto active = (item.data.id == _activeId);
		if (active) {
			p.fillRect(0, y, fullWidth, item.height, _st.textBgActive);
		}
		if (item.ripple) {
			item.ripple->paint(p, 0, y, fullWidth, &_st.rippleBg->c);
			if (item.ripple->empty()) {
				item.ripple = nullptr;
			}
		}
		const auto icon = (active ? item.data.iconActive : item.data.icon);
		const auto x = (fullWidth - icon->width()) / 2;
		icon->paint(p, x, y + item.data.iconTop, fullWidth);
		p.setPen(active ? _st.textFgActive : _st.textFg);
		item.text.drawElided(
			p,
			_st.margins.left(),
			y + _st.textTop,
			availableWidth,
			kMaxLabelLines,
			style::al_top);
		y += item.height;
	}
}

int SideBarMenu::countContentHeight(int width, int outerHeight) {
	const auto available = width - _st.margins.left() - _st.margins.right();
	const auto withoutText = _st.textTop + _st.bottomSkip;
	auto rows = _st.margins.top();
	for (auto &item : _items) {
		const auto fullTextHeight = item.text.countHeight(available);
		const auto textHeight = std::min(
			fullTextHeight,
			kMaxLabelLines * _st.style.font->height);
		item.top = rows;
		item.height = withoutText + textHeight;
		rows += item.height;
	}
	return std::max(rows + _st.margins.bottom(), outerHeight);
}

} // namespace Ui
