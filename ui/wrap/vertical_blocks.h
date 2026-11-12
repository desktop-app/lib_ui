// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/object_ptr.h"
#include "ui/text/text_entity.h"

namespace Ui {

class FlatLabel;
class RpWidget;
class VerticalLayout;

class VerticalBlocks final {
public:
	struct Args {
		int skip = 0;
		std::optional<int> duration;
		bool closingDivider = false;
	};
	struct Part {
		object_ptr<RpWidget> widget = { nullptr };
		rpl::producer<bool> shown;
	};
	struct Text {
		rpl::producer<TextWithEntities> text;
		rpl::producer<bool> shown;
		Fn<void(not_null<FlatLabel*>)> setup;
	};

	VerticalBlocks(not_null<VerticalLayout*> layout, Args args);

	void add(Part part);
	template <typename Widget>
	Widget *add(
			object_ptr<Widget> widget,
			rpl::producer<bool> shown = {}) {
		const auto raw = widget.data();
		add(Part{ std::move(widget), std::move(shown) });
		return raw;
	}
	void addSeparator();
	void addNote(Text text);
	void addAbout(Text text);

	[[nodiscard]] not_null<VerticalLayout*> layout() const;

private:
	struct State;

	not_null<State*> _state;

};

} // namespace Ui
