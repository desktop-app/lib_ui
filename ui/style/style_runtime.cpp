// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_runtime.h"

#include "base/options.h"
#include "base/algorithm.h"

namespace style {
namespace {

// Read once at startup. Default off — production behavior is identical to
// today (single global scale, no per-window context resolution).
base::options::toggle OptionApplyRuntimeScaleChanges({
	.id = "apply_runtime_scale_changes",
	.name = "Per-window runtime scale changes",
	.description = "Resolve interface scale per top-level window rather than"
		" globally. Required for per-monitor scaling. Disabled until the new"
		" style runtime migration is complete.",
	.restartRequired = true,
});

} // namespace

Registry::Registry() = default;

void Registry::add(
		const ModuleDescriptor *const *descriptors,
		int count) {
	Expects(!_frozen);

	_descriptors.reserve(_descriptors.size() + count);
	for (auto i = 0; i != count; ++i) {
		_descriptors.push_back(descriptors[i]);
	}
}

void Registry::freeze() {
	Expects(!_frozen);

	// Topological sort by dependencies. Detect cycles by visited markers.
	enum class Mark : uchar {
		Unvisited = 0,
		InProgress = 1,
		Done = 2,
	};
	auto marks = base::flat_map<const ModuleDescriptor*, Mark>();
	marks.reserve(_descriptors.size());
	for (const auto descriptor : _descriptors) {
		marks.emplace(descriptor, Mark::Unvisited);
	}

	_sorted.clear();
	_sorted.reserve(_descriptors.size());

	const auto visit = [&](
			const ModuleDescriptor *descriptor,
			auto &visit) -> void {
		const auto i = marks.find(descriptor);
		Assert(i != end(marks));
		if (i->second == Mark::Done) {
			return;
		}
		Assert(i->second != Mark::InProgress); // cycle
		i->second = Mark::InProgress;
		for (auto k = 0; k != descriptor->dependenciesCount; ++k) {
			visit(descriptor->dependencies[k], visit);
		}
		i->second = Mark::Done;
		_sorted.push_back(descriptor);
	};
	for (const auto descriptor : _descriptors) {
		visit(descriptor, visit);
	}

	for (auto i = 0; i != int(_sorted.size()); ++i) {
		_sorted[i]->id = ModuleId(i);
	}

	_frozen = true;
}

std::shared_ptr<const Modules> Registry::get(ScaleKey key) {
	Expects(_frozen);
	Expects(key.valid());

	for (auto i = _cache.begin(); i != _cache.end();) {
		if (auto alive = i->modules.lock()) {
			if (i->key == key) {
				return alive;
			}
			++i;
		} else {
			i = _cache.erase(i);
		}
	}

	auto result = std::make_shared<Modules>();
	result->_key = key;
	result->_owned.resize(_sorted.size());
	result->_raw.resize(_sorted.size());

	const auto context = BuildContext{ .key = key };

	for (const auto descriptor : _sorted) {
		const auto id = descriptor->id;
		Assert(id != kInvalidModuleId);
		Assert(descriptor->build != nullptr);

		auto built = descriptor->build(context, *result);
		result->_raw[id] = built.get();
		result->_owned[id] = std::move(built);
	}

	_cache.push_back({ key, result });
	return result;
}

Registry &GlobalRegistry() {
	static auto result = Registry();
	return result;
}

Context::Context(std::shared_ptr<const Modules> modules)
: _modules(std::move(modules)) {
}

void Context::setModules(std::shared_ptr<const Modules> modules) {
	_modules = std::move(modules);
}

Context &GlobalContext() {
	static auto result = Context();
	return result;
}

bool RuntimeScaleEnabled() {
	static const auto value = OptionApplyRuntimeScaleChanges.value();
	return value;
}

Context *ResolveContext(QWidget *owner) {
	// Phase 1: per-window resolution is not implemented yet. Always use the
	// global context. Phase 5 will read owner->window()'s ContextOwner
	// property when the toggle is on.
	(void)owner;
	return &GlobalContext();
}

} // namespace style
