// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/layers/layer_manager.h"

#include "ui/layers/show.h"
#include "ui/ui_utility.h"

namespace Ui {

class LayerManager::ManagerShow final : public Show {
public:
	explicit ManagerShow(not_null<LayerManager*> manager);
	~ManagerShow();

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

private:
	const base::weak_ptr<LayerManager> _manager;

};

LayerManager::ManagerShow::ManagerShow(not_null<LayerManager*> manager)
: _manager(manager.get()) {
}

LayerManager::ManagerShow::~ManagerShow() = default;

void LayerManager::ManagerShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const {
	using UniqueLayer = std::unique_ptr<Ui::LayerWidget>;
	using ObjectBox = object_ptr<Ui::BoxContent>;
	if (auto layerWidget = std::get_if<UniqueLayer>(&layer)) {
		if (const auto manager = _manager.get()) {
			manager->showLayer(std::move(*layerWidget), options, animated);
		}
	} else if (auto box = std::get_if<ObjectBox>(&layer)) {
		if (const auto manager = _manager.get()) {
			manager->showBox(std::move(*box), options, animated);
		}
	} else if (const auto manager = _manager.get()) {
		manager->hideAll(animated);
	}
}

not_null<QWidget*> LayerManager::ManagerShow::toastParent() const {
	const auto manager = _manager.get();

	Ensures(manager != nullptr);
	return manager->toastParent();
}

bool LayerManager::ManagerShow::valid() const {
	return (_manager.get() != nullptr);
}

LayerManager::ManagerShow::operator bool() const {
	return valid();
}

LayerManager::LayerManager(not_null<RpWidget*> widget)
: _widget(widget) {
}

void LayerManager::setStyleOverrides(
		const style::Box *boxSt,
		const style::Box *layerSt) {
	_boxSt = boxSt;
	_layerSt = layerSt;
	if (_layer) {
		_layer->setStyleOverrides(_boxSt, _layerSt);
	}
}

void LayerManager::setHideByBackgroundClick(bool hide) {
	_hideByBackgroundClick = hide;
	if (_layer) {
		_layer->setHideByBackgroundClick(hide);
	}
}

void LayerManager::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	ensureLayerCreated();
	_layer->showBox(std::move(box), options, animated);
	setFocus();
}

void LayerManager::showLayer(
		std::unique_ptr<LayerWidget> layer,
		LayerOptions options,
		anim::type animated) {
	ensureLayerCreated();
	_layer->showLayer(std::move(layer), options, animated);
	setFocus();
}

void LayerManager::hideAll(anim::type animated) {
	if (animated == anim::type::instant) {
		destroyLayer();
	} else if (_layer) {
		_layer->hideAll(animated);
	}
}

void LayerManager::raise() {
	if (_layer) {
		_layer->raise();
	}
}

bool LayerManager::setFocus() {
	if (!_layer) {
		return false;
	}
	_layer->setInnerFocus();
	return true;
}

std::shared_ptr<Show> LayerManager::uiShow() {
	if (!_cachedShow) {
		_cachedShow = std::make_shared<ManagerShow>(this);
	}
	return _cachedShow;
}

const LayerWidget *LayerManager::topShownLayer() const {
	return _layer ? _layer->topShownLayer() : nullptr;
}

void LayerManager::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer.emplace(_widget, crl::guard(this, [=] {
		return uiShow();
	}));
	_layer->setHideByBackgroundClick(_hideByBackgroundClick);
	_layer->setStyleOverrides(_boxSt, _layerSt);

	_layer->hideFinishEvents(
	) | rpl::filter([=] {
		return _layer != nullptr; // Last hide finish is sent from destructor.
	}) | rpl::start_with_next([=] {
		destroyLayer();
	}, _layer->lifetime());

	_layer->move(0, 0);
	_widget->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_layer->resize(size);
	}, _layer->lifetime());

	_layerShown = true;
}

void LayerManager::destroyLayer() {
	if (!_layer) {
		return;
	}

	auto layer = base::take(_layer);
	_layerShown = false;

	const auto resetFocus = Ui::InFocusChain(layer);
	if (resetFocus) {
		_widget->setFocus();
	}
	layer = nullptr;
}

} // namespace Ui
