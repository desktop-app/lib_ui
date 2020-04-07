// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast.h"

#include "ui/toast/toast_manager.h"
#include "ui/toast/toast_widget.h"

namespace Ui {
namespace Toast {
namespace {

QPointer<QWidget> DefaultParent;

} // namespace

Instance::Instance(
	const Config &config,
	not_null<QWidget*> widgetParent,
	const Private &)
: _hideAtMs(crl::now() + config.durationMs) {
	_widget = std::make_unique<internal::Widget>(widgetParent, config);
	_a_opacity.start(
		[=] { opacityAnimationCallback(); },
		0.,
		1.,
		st::toastFadeInDuration);
}

void SetDefaultParent(not_null<QWidget*> parent) {
	DefaultParent = parent;
}

void Show(not_null<QWidget*> parent, const Config &config) {
	const auto manager = internal::Manager::instance(parent);
	manager->addToast(std::make_unique<Instance>(
		config,
		parent,
		Instance::Private()));
}

void Show(const Config &config) {
	if (const auto parent = DefaultParent.data()) {
		Show(parent, config);
	}
}

void Show(not_null<QWidget*> parent, const QString &text) {
	auto config = Config();
	config.text = { text };
	Show(parent, config);
}

void Show(const QString &text) {
	auto config = Config();
	config.text = { text };
	Show(config);
}

void Instance::opacityAnimationCallback() {
	_widget->setShownLevel(_a_opacity.value(_hiding ? 0. : 1.));
	_widget->update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			hide();
		}
	}
}

void Instance::hideAnimated() {
	_hiding = true;
	_a_opacity.start([this] { opacityAnimationCallback(); }, 1., 0., st::toastFadeOutDuration);
}

void Instance::hide() {
	_widget->hide();
	_widget->deleteLater();
}

} // namespace Toast
} // namespace Ui
