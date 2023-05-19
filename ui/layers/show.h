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

namespace anim {
enum class type : uchar;
} // namespace anim

namespace Ui::Toast {
struct Config;
class Instance;
} // namespace Ui::Toast

namespace Ui {

class BoxContent;
class LayerWidget;

inline constexpr auto kZOrderBasic = 0;

class Show {
public:
	virtual ~Show() = 0;
	virtual void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<BoxContent>,
			std::unique_ptr<LayerWidget>> &&layer,
		LayerOptions options,
		anim::type animated) const = 0;
	[[nodiscard]] virtual not_null<QWidget*> toastParent() const = 0;
	[[nodiscard]] virtual bool valid() const = 0;
	virtual operator bool() const = 0;

	void showBox(
		object_ptr<BoxContent> content,
		LayerOptions options = LayerOption::KeepOther,
		anim::type animated = anim::type()) const;
	void showLayer(
		std::unique_ptr<LayerWidget> layer,
		LayerOptions options = LayerOption::KeepOther,
		anim::type animated = anim::type()) const;
	void hideLayer(anim::type animated = anim::type()) const;

	base::weak_ptr<Toast::Instance> showToast(Toast::Config &&config) const;
	base::weak_ptr<Toast::Instance> showToast(
		TextWithEntities &&text,
		crl::time duration = 0) const;
	base::weak_ptr<Toast::Instance> showToast(
		const QString &text,
		crl::time duration = 0) const;

	template <
		typename BoxType,
		typename = std::enable_if_t<std::is_base_of_v<BoxContent, BoxType>>>
	QPointer<BoxType> show(
			object_ptr<BoxType> content,
			LayerOptions options = LayerOption::KeepOther,
			anim::type animated = anim::type()) const {
		auto result = QPointer<BoxType>(content.data());
		showBox(std::move(content), options, animated);
		return result;
	}

private:
	mutable base::weak_ptr<Toast::Instance> _lastToast;

};

inline Show::~Show() = default;

} // namespace Ui
