// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/weak_ptr.h"
#include "ui/layers/layer_widget.h"

struct TextWithEntities;

namespace Ui::Toast {
struct Config;
class Instance;
} // namespace Ui::Toast

namespace Ui {

class BoxContent;

inline constexpr auto kZOrderBasic = 0;

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

	base::weak_ptr<Toast::Instance> showToast(Toast::Config &&config);
	base::weak_ptr<Toast::Instance> showToast(
		TextWithEntities &&text,
		crl::time duration = 0);
	base::weak_ptr<Toast::Instance> showToast(
		const QString &text,
		crl::time duration = 0);

private:
	base::weak_ptr<Toast::Instance> _lastToast;

};

inline Show::~Show() = default;

} // namespace Ui
