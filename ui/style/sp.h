// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

class QWidget;

namespace style {

class Context;
class Modules;
struct ModuleDescriptor;

struct ScaleKey {
	uint32 scale : 16 = 0;
	uint32 dpr : 8 = 0;
	uint32 _reserved : 8 = 0;

	[[nodiscard]] constexpr bool valid() const {
		return (scale != 0);
	}

	friend bool operator==(ScaleKey, ScaleKey) = default;
	friend auto operator<=>(ScaleKey, ScaleKey) = default;
};

inline constexpr auto kInvalidScaleKey = ScaleKey{};

using ModuleId = uint16;
inline constexpr ModuleId kInvalidModuleId = 0xFFFFu;

} // namespace style

namespace sv {

// Locator into a generated Module's storage. ModuleDescriptor address is
// stable for the program lifetime; offset is byte-offset of the value inside
// the descriptor's Module struct. As a migration bridge, `module == nullptr`
// marks a legacy locator: `offset` then carries the address of an existing
// `const style::T` object (typically a `st::*` global), and getRaw() returns
// it directly.
struct raw_value {
	const style::ModuleDescriptor *module = nullptr;
	quintptr offset = 0;
};

template <typename T>
struct value {
	using Type = T;

	constexpr value(
		const style::ModuleDescriptor *module,
		quintptr offset)
	: raw{ module, offset } {
	}

	// Legacy bridge: implicit conversion from `const style::T &` (typically
	// an `st::*` global). Lets unmigrated callers keep passing legacy style
	// references where a sv::T is now expected. Drop this ctor once nothing
	// outside generated code relies on it.
	/*implicit*/ value(const T &legacy)
	: raw{ nullptr, reinterpret_cast<quintptr>(&legacy) } {
	}

	[[nodiscard]] constexpr raw_value rawValue() const {
		return raw;
	}

	raw_value raw;
};

} // namespace sv

namespace sp {

// Non-template base storing the runtime cache. The first call to getRaw()
// (and any call after the bound Context's Modules pointer changes) re-resolves
// the raw void pointer to the value inside the current Modules.
class pointer_base {
public:
	pointer_base() = default;
	pointer_base(QWidget *owner, sv::raw_value value);

	[[nodiscard]] style::ScaleKey key() const;

	[[nodiscard]] int scale() const {
		return int(key().scale);
	}
	[[nodiscard]] int dpr() const {
		return int(key().dpr);
	}

	[[nodiscard]] sv::raw_value rawValue() const {
		return _value;
	}

protected:
	[[nodiscard]] const void *getRaw() const;

private:
	style::Context *_context = nullptr;
	sv::raw_value _value;

	mutable const style::Modules *_modules = nullptr;
	mutable const void *_raw = nullptr;
};

// Storage of pointer<T> does not depend on T, so T may be incomplete at the
// point pointer<T> is instantiated as a member. Member function bodies that
// reference T are instantiated only on use, by which time T must be complete
// at the call site.
template <typename SvType>
class pointer final : private pointer_base {
public:
	using pointer_base::key;
	using pointer_base::scale;
	using pointer_base::dpr;
	using pointer_base::rawValue;

	pointer() = default;

	pointer(QWidget *owner, SvType v)
	: pointer_base(owner, v.rawValue()) {
	}

	auto operator->() const {
		return static_cast<const typename SvType::Type *>(getRaw());
	}

	auto &operator*() const {
		return *operator->();
	}

	// Implicit conversion back to the locator, so a `sp::pointer` resolved
	// for one widget can be passed to a child constructor that takes the
	// same `sv::value` and the child rebinds it to its own context.
	operator SvType() const {
		const auto raw = rawValue();
		return SvType(raw.module, raw.offset);
	}
};

} // namespace sp
