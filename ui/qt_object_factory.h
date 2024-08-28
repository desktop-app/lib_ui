// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtWidgets/QWidget>

namespace Ui {
namespace details {

template <typename Value>
class AttachmentOwner : public QObject {
public:
	template <typename ...Args>
	AttachmentOwner(QObject *parent, Args &&...args)
	: QObject(parent)
	, _value(std::forward<Args>(args)...) {
	}

	gsl::not_null<Value*> value() {
		return &_value;
	}

private:
	Value _value;

};

} // namespace details

template <typename Value, typename Parent, typename ...Args>
inline Value *CreateChild(
		Parent parent,
		Args &&...args) {
	if constexpr (std::is_pointer_v<Parent>) {
		Expects(parent != nullptr);

		if constexpr (std::is_base_of_v<QObject, Value>) {
			return new Value(parent, std::forward<Args>(args)...);
		} else {
			return CreateChild<details::AttachmentOwner<Value>>(
				parent,
				std::forward<Args>(args)...)->value();
		}
	} else if constexpr (requires(const Parent & t) { t.get(); }) {
		return new Value(parent.get(), std::forward<Args>(args)...);
	} else {
		static_assert(requires(const Parent &t) { t.data(); });
		return new Value(parent.data(), std::forward<Args>(args)...);
	}
}

template <typename Value>
inline gsl::not_null<details::AttachmentOwner<std::decay_t<Value>>*> WrapAsQObject(
		gsl::not_null<QObject*> parent,
		Value &&value) {
	return CreateChild<details::AttachmentOwner<std::decay_t<Value>>>(
		parent.get(),
		std::forward<Value>(value));
}

} // namespace Ui
