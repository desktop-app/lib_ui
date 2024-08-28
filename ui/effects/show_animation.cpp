// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/show_animation.h"

#include "ui/effects/animations.h"
#include "ui/qt_weak_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_widgets.h"

namespace Ui::Animations {
namespace {

void AnimateWidgets(const Widgets &targets, bool show) {
	enum class Finish {
		Bad,
		Good,
	};
	struct Object {
		base::unique_qptr<Ui::RpWidget> container;
		QPointer<Ui::RpWidget> weakTarget;
	};
	struct State {
		rpl::event_stream<Finish> destroy;
		Ui::Animations::Simple animation;
		std::vector<Object> objects;
	};
	auto lifetime = std::make_shared<rpl::lifetime>();
	const auto state = lifetime->make_state<State>();

	const auto from = show ? 0. : 1.;
	const auto to = show ? 1. : 0.;

	for (const auto &target : targets) {
		state->objects.push_back({
			base::make_unique_q<Ui::RpWidget>(target->parentWidget()),
			Ui::MakeWeak(target),
		});

		const auto pixmap = Ui::GrabWidget(target);
		const auto raw = state->objects.back().container.get();

		raw->paintRequest(
		) | rpl::start_with_next([=] {
			QPainter p(raw);

			p.setOpacity(state->animation.value(to));
			p.drawPixmap(QPoint(), pixmap);
		}, raw->lifetime());

		target->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			raw->setGeometry(r);
		}, raw->lifetime());

		raw->show();

		if (!show) {
			target->hide();
		}
	}

	state->destroy.events(
	) | rpl::take(
		1
	) | rpl::start_with_next([=](Finish type) mutable {
		if (type == Finish::Good && show) {
			for (const auto &object : state->objects) {
				if (object.weakTarget) {
					object.weakTarget->show();
				}
			}
		}
		if (lifetime) {
			base::take(lifetime)->destroy();
		}
	}, *lifetime);

	state->animation.start(
		[=](auto value) {
			for (const auto &object : state->objects) {
				object.container->update();

				if (!object.weakTarget && show) {
					state->destroy.fire(Finish::Bad);
					return;
				}
			}
			if (value == to) {
				state->destroy.fire(Finish::Good);
			}
		},
		from,
		to,
		st::defaultToggle.duration);
}

} // namespace

void ShowWidgets(const Widgets &targets) {
	AnimateWidgets(targets, true);
}

void HideWidgets(const Widgets &targets) {
	AnimateWidgets(targets, false);
}

} // namespace Ui::Animations
