// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/animations.h"
#include "ui/text/text_entity.h"
#include "ui/click_handler.h"

namespace Ui {
namespace Toast {

namespace internal {
class Manager;
class Widget;
} // namespace internal

using ClickHandlerFilter = Fn<bool(const ClickHandlerPtr&, Qt::MouseButton)>;

inline constexpr auto kDefaultDuration = crl::time(1500);
struct Config {
	TextWithEntities text;
	QMargins padding;
	crl::time durationMs = kDefaultDuration;
	int minWidth = 0;
	int maxWidth = 0;
	int maxLines = 16;
	bool multiline = false;
	bool dark = false;
	ClickHandlerFilter filter;
};
void SetDefaultParent(not_null<QWidget*> parent);
void Show(not_null<QWidget*> parent, const Config &config);
void Show(const Config &config);
void Show(not_null<QWidget*> parent, const QString &text);
void Show(const QString &text);

class Instance {
	struct Private {
	};

public:
	Instance(
		const Config &config,
		not_null<QWidget*> widgetParent,
		const Private &);
	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;

	void hideAnimated();
	void hide();

private:
	void opacityAnimationCallback();

	bool _hiding = false;
	Ui::Animations::Simple _a_opacity;

	const crl::time _hideAtMs;

	// ToastManager should reset _widget pointer if _widget is destroyed.
	friend class internal::Manager;
	friend void Show(not_null<QWidget*> parent, const Config &config);
	std::unique_ptr<internal::Widget> _widget;

};

} // namespace Toast
} // namespace Ui
