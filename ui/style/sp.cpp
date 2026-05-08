// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/sp.h"

#include "ui/style/style_runtime.h"
#include "base/assertion.h"

namespace sp {

pointer_base::pointer_base(QWidget *owner, sv::raw_value value)
: _context(style::ResolveContext(owner))
, _value(value) {
}

style::ScaleKey pointer_base::key() const {
	Expects(_context != nullptr);

	const auto modules = _context->modulesPointer();
	return modules ? modules->key() : style::ScaleKey{};
}

const void *pointer_base::getRaw() const {
	Expects(_context != nullptr);

	if (!_value.module) {
		// Legacy bridge: offset is the address of an existing style object
		// (typically an `st::*` global). Return it directly; no caching
		// because there's nothing scale-dependent to invalidate.
		Expects(_value.offset != 0);
		return reinterpret_cast<const void*>(_value.offset);
	}

	const auto modules = _context->modulesPointer();
	Expects(modules != nullptr);

	if (_modules != modules) {
		const auto id = _value.module->id;
		Expects(id != style::kInvalidModuleId);

		const auto base = static_cast<const uchar*>(modules->raw(id));
		Expects(base != nullptr);

		_raw = base + _value.offset;
		_modules = modules;
	}
	return _raw;
}

} // namespace sp
