// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/sp.h"
#include "ui/style/style_core_scale.h"
#include "base/assertion.h"

#include <QtCore/QSize>
#include <QtCore/QPoint>
#include <QtCore/QMargins>

#include <memory>
#include <vector>

namespace style {

// Passed to a generated Module's Build function. Carries the ScaleKey plus
// helpers so generated code can write `context.px(8)` instead of relying on
// global pxN values.
struct BuildContext {
	ScaleKey key;

	template <typename T>
	[[nodiscard]] T px(T value) const {
		return ConvertScale(value, int(key.scale));
	}

	[[nodiscard]] QSize px(QSize value) const {
		return { px(value.width()), px(value.height()) };
	}

	[[nodiscard]] QPoint px(QPoint value) const {
		return { px(value.x()), px(value.y()) };
	}

	[[nodiscard]] QMargins px(QMargins value) const {
		return {
			px(value.left()),
			px(value.top()),
			px(value.right()),
			px(value.bottom()),
		};
	}
};

class Modules;

using ModuleBuildFn = std::shared_ptr<const void> (*)(
	const BuildContext &context,
	const Modules &alreadyBuilt);

// Generated. Address is stable; `id` is filled by Registry::freeze().
struct ModuleDescriptor {
	const char *name = nullptr;

	// Generated as a static span of pointers to other ModuleDescriptors.
	const ModuleDescriptor *const *dependencies = nullptr;
	int dependenciesCount = 0;

	ModuleBuildFn build = nullptr;

	mutable ModuleId id = kInvalidModuleId;
};

// Immutable resolved set of generated modules for one ScaleKey.
class Modules final {
public:
	Modules() = default;
	Modules(const Modules &) = delete;
	Modules &operator=(const Modules &) = delete;

	[[nodiscard]] ScaleKey key() const {
		return _key;
	}

	[[nodiscard]] const void *raw(ModuleId id) const {
		Expects(id < _raw.size());
		return _raw[id];
	}

private:
	friend class Registry;

	ScaleKey _key = kInvalidScaleKey;
	std::vector<std::shared_ptr<const void>> _owned;
	std::vector<const void*> _raw;
};

// Owns the descriptor set and a ScaleKey -> Modules cache.
class Registry final {
public:
	Registry();

	// Adds descriptors for later sorting + freezing. Pointers must remain
	// valid for the lifetime of the Registry (typically static-storage).
	void add(
		const ModuleDescriptor *const *descriptors,
		int count);

	template <int N>
	void add(const ModuleDescriptor *const (&descriptors)[N]) {
		add(descriptors, N);
	}

	// Topologically sort descriptors and assign dense ModuleIds.
	// Must be called after all add() calls and before any get().
	void freeze();

	// Builds (or returns cached) Modules for the given ScaleKey.
	[[nodiscard]] std::shared_ptr<const Modules> get(ScaleKey key);

private:
	std::vector<const ModuleDescriptor*> _descriptors;
	std::vector<const ModuleDescriptor*> _sorted;
	bool _frozen = false;

	// Strong (not weak) so once a Modules is built it stays alive for the
	// whole app run. This is required because consumers like
	// `Ui::Text::String` store raw `const style::TextStyle *` pointers into
	// the Modules' memory; freeing a Modules when its Context moves to a
	// different one would dangle those pointers and crash on next paint.
	// The memory cost is bounded by the number of distinct ScaleKeys the
	// app actually encounters during a session.
	struct CacheEntry {
		ScaleKey key = kInvalidScaleKey;
		std::shared_ptr<const Modules> modules;
	};
	std::vector<CacheEntry> _cache;
};

[[nodiscard]] Registry &GlobalRegistry();

// Mutable holder of "current modules". Owned by a top-level window (Phase 5)
// or by the global default (Phase 1).
class Context final {
public:
	Context() = default;
	explicit Context(std::shared_ptr<const Modules> modules);

	[[nodiscard]] const Modules *modulesPointer() const {
		return _modules.get();
	}

	[[nodiscard]] const std::shared_ptr<const Modules> &modulesPtr() const {
		return _modules;
	}

	[[nodiscard]] ScaleKey key() const {
		return _modules ? _modules->key() : kInvalidScaleKey;
	}

	void setModules(std::shared_ptr<const Modules> modules);

private:
	std::shared_ptr<const Modules> _modules;
};

[[nodiscard]] Context &GlobalContext();

// Identifier for the experimental option that gates per-window runtime
// scaling. Exposed so the experimental settings UI can list it.
inline constexpr auto kOptionApplyRuntimeScaleChanges
	= "apply_runtime_scale_changes";

// Read once at startup from the apply_runtime_scale_changes toggle. False in
// production by default — sp::pointer always resolves to the global Context's
// initial Modules, which never changes.
[[nodiscard]] bool RuntimeScaleEnabled();

// Owns a `style::Context` and lives as a child QObject of a top-level
// QWidget. Stored on the widget as a dynamic property so `ResolveContext`
// can find it via `owner->window()`. Auto-destroyed when the window dies.
class ContextOwner : public QObject {
public:
	ContextOwner(
		QObject *parent,
		std::shared_ptr<const Modules> modules);

	[[nodiscard]] Context *context() {
		return &_context;
	}

	// Set by `ScheduleScaleKeyRefresh`, cleared when the queued refresh
	// runs. Used to coalesce multiple incoming scale-change signals (Qt
	// `ScreenChangeInternal`, Qt `DevicePixelRatioChange`, native
	// `WM_DPICHANGED`) into one async recomputation per window.
	bool refreshPending = false;

private:
	Context _context;
};

// Attach a fresh ContextOwner to `window` (must be a top-level widget).
// Looks up / builds the Modules for `key` from GlobalRegistry. No-op if the
// window already has a ContextOwner. Returns the bound Context*.
Context *AttachContextToWindow(QWidget *window, ScaleKey key);

// Replace the modules of an already-attached ContextOwner with the ones for
// `key`. Used when the window's screen / DPR changes. No-op if the window
// has no ContextOwner.
void UpdateWindowScaleKey(QWidget *window, ScaleKey key);

// Compute a ScaleKey from a system-reported DPI and Qt's rounded DPR.
//
// `style::Scale()` is the user's chosen scale percentage as it appears in
// Settings, calibrated for the main screen at the moment the app was
// launched. For windows on the same scale as the main screen (the common
// case) we use Scale() directly. For windows on a screen with a different
// system DPI we adjust proportionally:
//
//     key.scale = round(Scale() * systemDpi / MainScreenDpi())
//     key.dpr   = qtDpr
//
// On main screen `systemDpi == MainScreenDpi()` so key.scale == Scale()
// (identity, no surprises). On a secondary screen with 225% system scale
// while main is 250%, the scale is reduced by 225/250, preserving the
// user-relative size.
[[nodiscard]] ScaleKey ComputeScaleKey(int systemDpi, int qtDpr);

// Helper that reads the window's current screen state
// (`screen()->logicalDotsPerInch()` for system DPI, `devicePixelRatio()`
// rounded for Qt's DPR) and feeds them into `ComputeScaleKey`.
[[nodiscard]] ScaleKey ComputeScaleKeyFor(QWidget *window);

// Queue an async recomputation of the window's ScaleKey. Multiple calls
// before the queued callback fires are coalesced into one. By the time the
// callback runs, Qt has finished any in-flight processing of native
// scale-change events, so `ComputeScaleKeyFor(window)` reads consistent,
// settled values for both system DPI and Qt's DPR — avoiding the race
// where a fast native handler computes from a pre-event DPR.
void ScheduleScaleKeyRefresh(QWidget *window);

// Resolves the Context for an owner widget. Toggle off (production default):
// always returns &GlobalContext(). Toggle on: walks `owner->window()` and
// returns its ContextOwner's context if attached, else falls back to
// &GlobalContext().
[[nodiscard]] Context *ResolveContext(QWidget *owner);

} // namespace style
