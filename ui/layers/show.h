// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/layers/layer_widget.h"

namespace Ui {

class BoxContent;

class Show {
public:
	virtual ~Show() = 0;
	virtual void showBox(
		object_ptr<BoxContent> content,
		LayerOptions options = LayerOption::KeepOther) const = 0;
	virtual void hideLayer() const = 0;
	[[nodiscard]] virtual not_null<QWidget*> toastParent() const = 0;
	[[nodiscard]] virtual bool valid() const = 0;
	virtual operator bool() const = 0;
};

inline Show::~Show() = default;

using ShowPtr = std::shared_ptr<Show>;


} // namespace Ui
