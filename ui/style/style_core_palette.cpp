// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_palette.h"

#include "ui/style/style_palette_colorizer.h"

namespace style {
namespace {

struct MainOverrideState {
	const palette *active = nullptr;
	const palette *shadowFor = nullptr;
	int shadowVersion = -1;
	std::vector<int> carried;
	std::unique_ptr<palette> shadow;
};

[[nodiscard]] MainOverrideState &MainOverride() {
	static auto result = MainOverrideState(); // main thread only
	return result;
}

void InvalidateMainOverride() {
	Expects(!MainOverride().active);

	MainOverride().shadowFor = nullptr;
}

} // namespace

struct palette::FinalizeHelper {
	not_null<const colorizer*> with;
	base::flat_set<int> ignoreKeys;
	base::flat_map<
		int,
		std::pair<colorizer::Color, colorizer::Color>> keepContrast;
};

palette::palette() = default;

palette::~palette() {
	clear();
}

int palette::indexOfColor(style::color c) const {
	auto start = data(0);
	if (c._data >= start && c._data < start + kCount) {
		return static_cast<int>(c._data - start);
	}
	return -1;
}

color palette::colorAtIndex(int index) const {
	Expects(index >= 0 && index < kCount);
	Expects(_ready);

	return _colors[index];
}

void palette::finalize(const colorizer &with) {
	if (_ready) return;
	_ready = true;

	_finalizeHelper = PrepareFinalizeHelper(with);
	palette_data::finalize(*this);
	_finalizeHelper = nullptr;
}

void palette::finalize() {
	finalize(colorizer());
}

palette &palette::operator=(const palette &other) {
	auto wasReady = _ready;
	for (int i = 0; i != kCount; ++i) {
		if (other._status[i] != Status::Initial) {
			if (_status[i] == Status::Initial) {
				new (data(i)) internal::ColorData(*other.data(i));
			} else {
				*data(i) = *other.data(i);
			}
			_status[i] = Status::Loaded;
		} else if (_status[i] != Status::Initial) {
			data(i)->~ColorData();
			_status[i] = Status::Initial;
			_ready = false;
		}
	}
	if (wasReady && !_ready) {
		finalize();
	}
	return *this;
}

QByteArray palette::save() const {
	if (!_ready) {
		const_cast<palette*>(this)->finalize();
	}

	auto result = QByteArray(kCount * 4, Qt::Uninitialized);
	for (auto i = 0, index = 0; i != kCount; ++i) {
		result[index++] = static_cast<uchar>(data(i)->c.red());
		result[index++] = static_cast<uchar>(data(i)->c.green());
		result[index++] = static_cast<uchar>(data(i)->c.blue());
		result[index++] = static_cast<uchar>(data(i)->c.alpha());
	}
	return result;
}

bool palette::load(const QByteArray &cache) {
	if (cache.size() != kCount * 4) {
		return false;
	}

	auto p = reinterpret_cast<const uchar*>(cache.constData());
	for (auto i = 0; i != kCount; ++i) {
		setData(i, { p[i * 4 + 0], p[i * 4 + 1], p[i * 4 + 2], p[i * 4 + 3] });
	}
	return true;
}

palette::SetResult palette::setColor(QLatin1String name, uchar r, uchar g, uchar b, uchar a) {
	auto nameIndex = internal::GetPaletteIndex(name);
	if (nameIndex < 0) return SetResult::KeyNotFound;
	auto duplicate = (_status[nameIndex] != Status::Initial);

	setData(nameIndex, { r, g, b, a });
	return duplicate ? SetResult::Duplicate : SetResult::Ok;
}

palette::SetResult palette::setColor(QLatin1String name, const QColor &color) {
	auto r = 0;
	auto g = 0;
	auto b = 0;
	auto a = 0;
	color.getRgb(&r, &g, &b, &a);
	return setColor(name, uchar(r), uchar(g), uchar(b), uchar(a));
}

palette::SetResult palette::setColor(QLatin1String name, QLatin1String from) {
	const auto nameIndex = internal::GetPaletteIndex(name);
	if (nameIndex < 0) {
		return SetResult::KeyNotFound;
	}
	const auto duplicate = (_status[nameIndex] != Status::Initial);

	const auto fromIndex = internal::GetPaletteIndex(from);
	if (fromIndex < 0 || _status[fromIndex] != Status::Loaded) {
		return SetResult::ValueNotFound;
	}

	setData(nameIndex, *data(fromIndex));
	return duplicate ? SetResult::Duplicate : SetResult::Ok;
}

void palette::reset(const colorizer &with) {
	clear();
	finalize(with);
}

void palette::reset() {
	clear();
	finalize();
}

void palette::clear() {
	for (auto i = 0; i != kCount; ++i) {
		if (_status[i] != Status::Initial) {
			data(i)->~ColorData();
			_status[i] = Status::Initial;
			_ready = false;
		}
	}
}

void palette::compute(int index, int fallbackIndex, TempColorData value) {
	if (_status[index] == Status::Initial) {
		if (fallbackIndex >= 0 && _status[fallbackIndex] == Status::Loaded) {
			_status[index] = Status::Loaded;
			new (data(index)) internal::ColorData(*data(fallbackIndex));
		} else {
			if (!_finalizeHelper
				|| _finalizeHelper->ignoreKeys.contains(index)) {
				_status[index] = Status::Created;
			} else {
				const auto &with = *_finalizeHelper->with;
				const auto i = _finalizeHelper->keepContrast.find(index);
				if (i == end(_finalizeHelper->keepContrast)) {
					colorize(value.r, value.g, value.b, with);
				} else {
					colorize(i->second, value.r, value.g, value.b, with);
				}
				_status[index] = Status::Loaded;
			}
			new (data(index)) internal::ColorData(value.r, value.g, value.b, value.a);
		}
	}
}

void palette::setData(int index, const internal::ColorData &value) {
	if (_status[index] == Status::Initial) {
		new (data(index)) internal::ColorData(value);
	} else {
		*data(index) = value;
	}
	_status[index] = Status::Loaded;
}

auto palette::PrepareFinalizeHelper(const colorizer &with)
-> std::unique_ptr<FinalizeHelper> {
	if (!with) {
		return nullptr;
	}
	auto result = std::make_unique<FinalizeHelper>(FinalizeHelper{
		.with = &with,
	});
	result->ignoreKeys.reserve(with.ignoreKeys.size() + 1);
	result->ignoreKeys.emplace(0);
	for (const auto &key : with.ignoreKeys) {
		if (const auto index = internal::GetPaletteIndex(key); index > 0) {
			result->ignoreKeys.emplace(index);
		}
	}
	for (const auto &[key, contrast] : with.keepContrast) {
		if (const auto index = internal::GetPaletteIndex(key); index > 0) {
			result->keepContrast.emplace(index, contrast);
		}
	}
	return result;
}

void palette::SwitchMainOverride(const palette *target) {
	auto &state = MainOverride();
	if (state.active == target) {
		return;
	}
	auto &main = const_cast<palette&>(*main_palette::get());
	const auto exchange = [&] {
		auto &shadow = *state.shadow;
		for (const auto index : state.carried) {
			auto &slot = *main.data(index);
			auto &other = *shadow.data(index);
			std::swap(slot.c, other.c);
			slot.p.swap(other.p);
			slot.b.swap(other.b);
		}
	};
	if (state.active) {
		exchange();
		state.active = nullptr;
	}
	if (!target) {
		return;
	}
	Expects(target->_ready);

	const auto version = PaletteVersion();
	if (state.shadowFor != target || state.shadowVersion != version) {
		if (!state.shadow) {
			state.shadow = std::make_unique<palette>();
		}
		state.carried.clear();
		for (auto i = 0; i != kCount; ++i) {
			if (main.data(i)->c != target->data(i)->c) {
				state.carried.push_back(i);
				state.shadow->setData(i, *target->data(i));
			}
		}
		state.shadowFor = target;
		state.shadowVersion = version;
	}
	exchange();
	state.active = target;
}

namespace main_palette {
namespace {

palette &GetMutable() {
	return const_cast<palette&>(*get());
}

} // namespace

QByteArray save() {
	return GetMutable().save();
}

bool load(const QByteArray &cache) {
	InvalidateMainOverride();
	if (GetMutable().load(cache)) {
		style::internal::ResetIcons();
		return true;
	}
	return false;
}

palette::SetResult setColor(QLatin1String name, uchar r, uchar g, uchar b, uchar a) {
	InvalidateMainOverride();
	return GetMutable().setColor(name, r, g, b, a);
}

palette::SetResult setColor(QLatin1String name, QLatin1String from) {
	InvalidateMainOverride();
	return GetMutable().setColor(name, from);
}

void apply(const palette &other) {
	InvalidateMainOverride();
	GetMutable() = other;
	style::internal::ResetIcons();
}

void reset() {
	InvalidateMainOverride();
	GetMutable().reset();
	style::internal::ResetIcons();
}

void reset(const colorizer &with) {
	InvalidateMainOverride();
	GetMutable().reset(with);
	style::internal::ResetIcons();
}

int indexOfColor(color c) {
	return GetMutable().indexOfColor(c);
}

Override::Override(const palette *with)
: _was(MainOverride().active) {
	palette::SwitchMainOverride(with);
}

Override::~Override() {
	palette::SwitchMainOverride(_was);
}

const palette *CurrentOverride() {
	return MainOverride().active;
}

} // namespace main_palette
} // namespace style
