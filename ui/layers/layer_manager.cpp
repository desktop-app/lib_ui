// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/layers/layer_manager.h"

namespace Ui {

LayerManager::LayerManager(not_null<RpWidget*> widget) : _widget(widget) {
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

const LayerWidget *LayerManager::topShownLayer() const {
	return _layer ? _layer->topShownLayer() : nullptr;
}

void LayerManager::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer.emplace(_widget);
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
}

void LayerManager::destroyLayer() {
	if (!_layer) {
		return;
	}

	auto layer = base::take(_layer);
	const auto resetFocus = Ui::InFocusChain(layer);
	if (resetFocus) {
		_widget->setFocus();
	}
	layer = nullptr;
}

} // namespace Ui
