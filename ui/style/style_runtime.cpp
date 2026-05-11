// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_runtime.h"

#include "base/options.h"
#include "base/algorithm.h"

#include <crl/crl_on_main.h>

#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtGui/QScreen>
#include <QtWidgets/QWidget>

#include <cmath>

namespace style {
namespace {

// Read once at startup. Default off — production behavior is identical to
// today (single global scale, no per-window context resolution).
base::options::toggle OptionApplyRuntimeScaleChanges({
	.id = kOptionApplyRuntimeScaleChanges,
	.name = "Per-window runtime scale changes",
	.description = "Resolve interface scale per top-level window rather than"
		" globally. Required for per-monitor scaling. Disabled until the new"
		" style runtime migration is complete.",
	.restartRequired = true,
});

constexpr auto kContextProperty = "ui::style::context";

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

	for (const auto &entry : _cache) {
		if (entry.key == key) {
			return entry.modules;
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

ContextOwner::ContextOwner(
	QObject *parent,
	std::shared_ptr<const Modules> modules)
: QObject(parent)
, _context(std::move(modules)) {
}

namespace {

[[nodiscard]] ContextOwner *FindContextOwner(QWidget *window) {
	if (!window) {
		return nullptr;
	}
	const auto value = window->property(kContextProperty);
	// Property is set only by AttachContextToWindow with a ContextOwner*,
	// so a static_cast is sufficient (no Q_OBJECT / no qobject_cast).
	return static_cast<ContextOwner*>(value.value<QObject*>());
}

} // namespace

Context *AttachContextToWindow(QWidget *window, ScaleKey key) {
	Expects(window != nullptr);
	Expects(window->isWindow());

	if (const auto existing = FindContextOwner(window)) {
		return existing->context();
	}
	auto modules = GlobalRegistry().get(key);
	const auto owner = new ContextOwner(window, std::move(modules));
	window->setProperty(kContextProperty,
		QVariant::fromValue<QObject*>(owner));
	return owner->context();
}

void UpdateWindowScaleKey(QWidget *window, ScaleKey key) {
	const auto owner = FindContextOwner(window);
	if (!owner || owner->context()->key() == key) {
		return;
	}
	owner->context()->setModules(GlobalRegistry().get(key));

	// Trigger a repaint of every descendant. sp::pointer's cache check
	// `_modules != modules` will detect the swap on next `_st->...` access
	// and re-resolve. Layout-sensitive widgets whose geometry is computed
	// from `_st` won't automatically resize — that needs a styleChanged
	// event handler in each widget; for now they keep their current size
	// and just repaint with new field values.
	window->update();
	const auto descendants = window->findChildren<QWidget*>();
	for (const auto child : descendants) {
		child->update();
	}
}

Context *ResolveContext(QWidget *owner) {
	if (!RuntimeScaleEnabled() || !owner) {
		return &GlobalContext();
	}
	if (const auto attached = FindContextOwner(owner->window())) {
		return attached->context();
	}
	return &GlobalContext();
}

ScaleKey ComputeScaleKey(int systemDpi, int qtDpr) {
	const auto safeDpr = std::max(qtDpr, 1);
	const auto baseline = MainScreenDpi();
	// On the main screen `systemDpi == baseline` so this is a no-op and
	// the window gets exactly the user's chosen `style::Scale()` (matching
	// the value visible in Settings). On other screens the scale is
	// adjusted proportionally so the user-relative size is preserved.
	const auto scaled = (baseline > 0)
		? std::lround(double(Scale()) * systemDpi / double(baseline))
		: Scale();
	return MakeScaleKey(int(scaled), safeDpr);
}

ScaleKey ComputeScaleKeyFor(QWidget *window) {
	const auto screen = window ? window->screen() : nullptr;
	if (!screen) {
		return MakeScaleKey(Scale(), 1);
	}
	const auto qtDpr = std::max(
		int(std::round(screen->devicePixelRatio())),
		1);
	const auto systemDpi = int(std::lround(screen->logicalDotsPerInch()));
	return ComputeScaleKey(systemDpi, qtDpr);
}

void ScheduleScaleKeyRefresh(QWidget *window) {
	const auto owner = FindContextOwner(window);
	if (!owner || owner->refreshPending) {
		return;
	}
	owner->refreshPending = true;
	// `crl::on_main(window, ...)` posts to the main thread and cancels if
	// `window` is destroyed before the callback runs. By the time the queued
	// callback fires, Qt has finished processing whatever event led here, so
	// `screen()->logicalDotsPerInch()` / `devicePixelRatio()` are settled.
	crl::on_main(window, [window, owner] {
		owner->refreshPending = false;
		UpdateWindowScaleKey(window, ComputeScaleKeyFor(window));
	});
}

} // namespace style
