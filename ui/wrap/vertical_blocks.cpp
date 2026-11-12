// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/wrap/vertical_blocks.h"

#include "base/invoke_queued.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>
#include <QtGui/QPainter>

namespace Ui {
namespace {

enum class Neighbor {
	None,
	Content,
	Divider,
};

class Divider final : public RpWidget {
public:
	Divider(QWidget *parent, int skip, bool text);

	[[nodiscard]] FlatLabel *label() const;
	bool setNeighbors(Neighbor above, Neighbor below);

	QAccessible::Role accessibilityRole() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] int topSkip() const;
	[[nodiscard]] int bottomSkip() const;

	const style::DividerBar &_st;
	const int _skip = 0;
	object_ptr<FlatLabel> _label = { nullptr };
	Neighbor _above = Neighbor::Content;
	Neighbor _below = Neighbor::Content;
	bool _resizing = false;

};

Divider::Divider(QWidget *parent, int skip, bool text)
: RpWidget(parent)
, _st(st::defaultDividerLabel.bar)
, _skip(skip) {
	if (text) {
		_label.create(this, st::defaultDividerLabel.label);
		_label->heightValue() | rpl::on_next([=] {
			if (!_resizing) {
				resizeToWidth(widthNoMargins());
			}
		}, _label->lifetime());
	}
	resizeToWidth(widthNoMargins());
}

FlatLabel *Divider::label() const {
	return _label.data();
}

bool Divider::setNeighbors(Neighbor above, Neighbor below) {
	if (_above == above && _below == below) {
		return false;
	}
	_above = above;
	_below = below;
	resizeToWidth(widthNoMargins());
	update();
	return true;
}

QAccessible::Role Divider::accessibilityRole() {
	return _label
		? RpWidget::accessibilityRole()
		: QAccessible::Role::Separator;
}

int Divider::topSkip() const {
	return (_above == Neighbor::Content) ? _skip : 0;
}

int Divider::bottomSkip() const {
	return (_below == Neighbor::Content) ? _skip : 0;
}

int Divider::resizeGetHeight(int newWidth) {
	const auto top = topSkip();
	if (!_label) {
		return top + st::boxDividerHeight + bottomSkip();
	}
	const auto &padding = st::defaultBoxDividerLabelPadding;
	_resizing = true;
	_label->resizeToWidth(newWidth - padding.left() - padding.right());
	_resizing = false;
	_label->moveToLeft(padding.left(), top + padding.top(), newWidth);
	return top
		+ padding.top()
		+ _label->height()
		+ ((_below == Neighbor::Divider) ? padding.top() : padding.bottom())
		+ bottomSkip();
}

void Divider::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto top = topSkip();
	const auto bottom = height() - bottomSkip();
	p.fillRect(0, top, width(), bottom - top, _st.bg);
	if (_above != Neighbor::Divider) {
		_st.top.fill(p, QRect(0, top, width(), _st.top.height()));
	}
	if (_below != Neighbor::Divider) {
		_st.bottom.fill(p, QRect(
			0,
			bottom - _st.bottom.height(),
			width(),
			_st.bottom.height()));
	}
}

} // namespace

struct VerticalBlocks::State {
	enum class Kind {
		Part,
		Note,
		About,
		Separator,
	};
	struct Item {
		Kind kind = Kind::Part;
		QPointer<RpWidget> part;
		SlideWrap<RpWidget> *slide = nullptr;
		QPointer<SlideWrap<Divider>> divider;
		bool shown = true;
		bool filled = false;
		bool visible = false;
		Neighbor above = Neighbor::None;
		Neighbor below = Neighbor::None;
	};

	State(not_null<VerticalLayout*> layout, Args args);

	void addPart(Part &&part);
	int addDivider(Kind kind);
	void addText(Kind kind, Text &&text);
	[[nodiscard]] Fn<void(bool)> setter(int index, bool Item::*field);
	[[nodiscard]] bool present(const Item &item) const;
	void refresh();
	void compute();
	void apply();

	const not_null<VerticalLayout*> layout;
	const Args args;
	std::vector<Item> items;
	bool building = true;
	bool refreshing = false;
	bool refreshAgain = false;
	bool relayoutQueued = false;
};

VerticalBlocks::State::State(not_null<VerticalLayout*> layout, Args args)
: layout(layout)
, args(args) {
	InvokeQueued(layout, [=] {
		refresh();
		building = false;
	});
}

void VerticalBlocks::State::addPart(Part &&part) {
	const auto raw = layout->add(std::move(part.widget));
	const auto index = int(items.size());
	const auto slide = dynamic_cast<SlideWrap<RpWidget>*>(raw);
	items.push_back({ .kind = Kind::Part, .part = raw, .slide = slide });
	if (slide) {
		slide->toggledValue() | rpl::on_next([=] {
			refresh();
		}, raw->lifetime());
	}
	if (part.shown) {
		std::move(
			part.shown
		) | rpl::on_next(setter(index, &Item::shown), raw->lifetime());
	}
	raw->heightValue() | rpl::map([](int height) {
		return (height > 0);
	}) | rpl::distinct_until_changed() | rpl::on_next([=] {
		refresh();
	}, raw->lifetime());
}

int VerticalBlocks::State::addDivider(Kind kind) {
	const auto text = (kind != Kind::Separator);
	const auto wrap = layout->add(object_ptr<SlideWrap<Divider>>(
		layout,
		object_ptr<Divider>(layout, args.skip, text)));
	if (args.duration) {
		wrap->setDuration(*args.duration);
	}
	wrap->hide(anim::type::instant);
	items.push_back({ .kind = kind, .divider = wrap });
	return int(items.size()) - 1;
}

void VerticalBlocks::State::addText(Kind kind, Text &&text) {
	const auto index = addDivider(kind);
	const auto wrap = items[index].divider;
	const auto label = wrap->entity()->label();
	if (text.setup) {
		text.setup(label);
	}
	std::move(
		text.text
	) | rpl::on_next([=](const TextWithEntities &value) {
		const auto filled = !value.empty();
		if (filled) {
			label->setMarkedText(value);
		}
		setter(index, &Item::filled)(filled);
	}, wrap->lifetime());
	if (text.shown) {
		std::move(
			text.shown
		) | rpl::on_next(setter(index, &Item::shown), wrap->lifetime());
	}
	refresh();
}

Fn<void(bool)> VerticalBlocks::State::setter(int index, bool Item::*field) {
	return [=](bool value) {
		if (items[index].*field != value) {
			items[index].*field = value;
			refresh();
		}
	};
}

// Resize events of hidden widgets are postponed, so heights are read here.
bool VerticalBlocks::State::present(const Item &item) const {
	if (!item.shown) {
		return false;
	} else if (item.kind != Kind::Part) {
		return item.filled;
	}
	const auto part = item.part.data();
	return part
		&& (!item.slide || item.slide->toggled())
		&& (part->height() > 0);
}

void VerticalBlocks::State::refresh() {
	if (refreshing) {
		refreshAgain = true;
		return;
	}
	refreshing = true;
	const auto guard = gsl::finally([&] { refreshing = false; });
	do {
		refreshAgain = false;
		compute();
		apply();
	} while (refreshAgain);
}

void VerticalBlocks::State::compute() {
	auto blockShown = false;
	auto last = Neighbor::None;
	auto lastText = -1;
	auto candidate = -1;
	const auto count = int(items.size());
	for (auto i = 0; i != count; ++i) {
		auto &item = items[i];
		item.visible = false;
		item.above = item.below = Neighbor::None;
		const auto shown = present(item);
		switch (item.kind) {
		case Kind::Part:
			if (!shown) {
				break;
			}
			item.visible = true;
			if (candidate >= 0) {
				auto &separator = items[candidate];
				separator.visible = true;
				separator.above = separator.below = Neighbor::Content;
				candidate = -1;
			} else if (lastText >= 0) {
				items[lastText].below = Neighbor::Content;
			}
			blockShown = true;
			last = Neighbor::Content;
			lastText = -1;
			break;
		case Kind::Note:
		case Kind::About:
			if (!shown || (item.kind == Kind::About && !blockShown)) {
				break;
			}
			item.visible = true;
			item.above = last;
			if (lastText >= 0) {
				items[lastText].below = Neighbor::Divider;
			}
			last = Neighbor::Divider;
			lastText = i;
			candidate = -1;
			break;
		case Kind::Separator:
			blockShown = false;
			if (last == Neighbor::Content && candidate < 0) {
				candidate = i;
			}
			break;
		}
	}
	if (candidate >= 0 && args.closingDivider) {
		items[candidate].visible = true;
		items[candidate].above = Neighbor::Content;
	}
}

void VerticalBlocks::State::apply() {
	const auto animated = building
		? anim::type::instant
		: anim::type::normal;
	auto changed = false;
	for (const auto &item : items) {
		const auto wrap = item.divider.data();
		if (!wrap) {
			continue;
		} else if (item.visible
			&& wrap->entity()->setNeighbors(item.above, item.below)) {
			SendPendingMoveResizeEvents(wrap);
		}
		if (wrap->toggled() != item.visible) {
			wrap->toggle(item.visible, animated);
			changed = true;
		}
	}
	if (!changed || relayoutQueued) {
		return;
	}
	relayoutQueued = true;
	// A toggle landing inside a layout resize pass is skipped there.
	InvokeQueued(layout, [=] {
		relayoutQueued = false;
		layout->resizeToWidth(layout->widthNoMargins());
	});
}

VerticalBlocks::VerticalBlocks(not_null<VerticalLayout*> layout, Args args)
: _state(layout->lifetime().make_state<State>(layout, args)) {
}

void VerticalBlocks::add(Part part) {
	if (part.widget) {
		_state->addPart(std::move(part));
	}
}

void VerticalBlocks::addSeparator() {
	const auto state = _state;
	const auto &items = state->items;
	if (!items.empty() && items.back().kind == State::Kind::Separator) {
		return;
	}
	state->addDivider(State::Kind::Separator);
	state->refresh();
}

void VerticalBlocks::addNote(Text text) {
	_state->addText(State::Kind::Note, std::move(text));
}

void VerticalBlocks::addAbout(Text text) {
	_state->addText(State::Kind::About, std::move(text));
	addSeparator();
}

not_null<VerticalLayout*> VerticalBlocks::layout() const {
	return _state->layout;
}

} // namespace Ui
