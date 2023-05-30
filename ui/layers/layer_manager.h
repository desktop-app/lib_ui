// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/weak_ptr.h"
#include "ui/layers/layer_widget.h"

#include <QtCore/QMargins>

namespace style {
struct Box;
} // namespace style

namespace Ui {

class BoxContent;
class RpWidget;
class Show;

class LayerManager final : public base::has_weak_ptr {
public:
	explicit LayerManager(not_null<RpWidget*> widget);

	void setStyleOverrides(
		const style::Box *boxSt,
		const style::Box *layerSt);

	void setHideByBackgroundClick(bool hide);
	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options = LayerOption::KeepOther,
		anim::type animated = anim::type::normal);
	void showLayer(
		std::unique_ptr<LayerWidget> layer,
		LayerOptions options = LayerOption::KeepOther,
		anim::type animated = anim::type::normal);
	void hideAll(anim::type animated = anim::type::normal);
	void raise();
	bool setFocus();

	[[nodiscard]] rpl::producer<bool> layerShownValue() const {
		return _layerShown.value();
	}

	[[nodiscard]] not_null<Ui::RpWidget*> toastParent() const {
		return _widget;
	}
	[[nodiscard]] const LayerWidget *topShownLayer() const;

	[[nodiscard]] std::shared_ptr<Show> uiShow();

private:
	class ManagerShow;

	void ensureLayerCreated();
	void destroyLayer();

	const not_null<RpWidget*> _widget;
	base::unique_qptr<LayerStackWidget> _layer;
	std::shared_ptr<ManagerShow> _cachedShow;
	rpl::variable<bool> _layerShown;

	const style::Box *_boxSt = nullptr;
	const style::Box *_layerSt = nullptr;
	bool _hideByBackgroundClick = false;

};

} // namespace Ui
