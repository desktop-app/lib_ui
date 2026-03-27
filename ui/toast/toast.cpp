// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/toast/toast.h"

#include "ui/toast/toast_manager.h"
#include "ui/toast/toast_widget.h"
#include "styles/style_widgets.h"

#include <vector>

namespace Ui::Toast {
namespace {

QPointer<QWidget> DefaultParent;

[[nodiscard]] auto &IconFactories() {
	static auto result = std::vector<ToastIconFactory>();
	return result;
}

} // namespace

Instance::Instance(
	not_null<QWidget*> widgetParent,
	Config &&config,
	const Private &)
: _st(config.st)
, _hideAt(config.infinite
	? 0
	: (crl::now() + (config.duration ? config.duration : kDefaultDuration)))
, _sliding(config.attach != RectPart::None)
, _widget(
	std::make_unique<internal::Widget>(
		widgetParent,
		std::move(config))) {
	_shownAnimation.start(
		[=] { shownAnimationCallback(); },
		0.,
		1.,
		_sliding ? _st->durationSlide : _st->durationFadeIn);
}

void SetDefaultParent(not_null<QWidget*> parent) {
	DefaultParent = parent;
}

void AddIconFactory(ToastIconFactory factory) {
	if (!factory) {
		return;
	}

	IconFactories().push_back(std::move(factory));
}

object_ptr<RpWidget> internal::MakeIconByFactory(
		not_null<RpWidget*> parent,
		const Config &config) {
	const auto &factories = IconFactories();
	for (auto i = factories.crbegin(); i != factories.crend(); ++i) {
		if (auto result = (*i)(parent, config)) {
			return result;
		}
	}
	return { nullptr };
}

base::weak_ptr<Instance> Show(
		not_null<QWidget*> parent,
		Config &&config) {
	const auto manager = internal::Manager::instance(parent);
	return manager->addToast(std::make_unique<Instance>(
		parent,
		std::move(config),
		Instance::Private()));
}

base::weak_ptr<Instance> Show(Config &&config) {
	if (const auto parent = DefaultParent.data()) {
		return Show(parent, std::move(config));
	}
	return nullptr;
}

base::weak_ptr<Instance> Show(
		not_null<QWidget*> parent,
		const QString &text) {
	return Show(parent, { .text = { text }, .st = &st::defaultToast });
}

base::weak_ptr<Instance> Show(const QString &text) {
	return Show({ .text = { text }, .st = &st::defaultToast });
}

void Instance::shownAnimationCallback() {
	_widget->setShownLevel(_shownAnimation.value(_hiding ? 0. : 1.));
	if (!_shownAnimation.animating()) {
		if (_hiding) {
			hide();
		}
	}
}

void Instance::hideAnimated() {
	_hiding = true;
	_shownAnimation.start(
		[=] { shownAnimationCallback(); },
		1.,
		0.,
		_sliding ? _st->durationSlide : _st->durationFadeOut);
}

void Instance::hide() {
	_widget->hide();
	_widget->deleteLater();
}

not_null<RpWidget*> Instance::widget() const {
	return _widget.get();
}

} // namespace Ui::Toast
