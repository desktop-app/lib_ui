// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/style/style_core.h"
#include "styles/palette.h"

namespace style {

struct colorizer;

class palette : public palette_data {
public:
	palette();
	palette(const palette &other) = delete;
	palette &operator=(const palette &other);
	~palette();

	QByteArray save() const;
	bool load(const QByteArray &cache);

	enum class SetResult {
		Ok,
		KeyNotFound,
		ValueNotFound,
		Duplicate,
	};
	SetResult setColor(QLatin1String name, uchar r, uchar g, uchar b, uchar a);
	SetResult setColor(QLatin1String name, const QColor &color);
	SetResult setColor(QLatin1String name, QLatin1String from);
	void reset(const colorizer &with);
	void reset();

	// Created not inited, should be finalized before usage.
	void finalize(const colorizer &with);
	void finalize();

	int indexOfColor(color c) const;
	color colorAtIndex(int index) const;

private:
	struct FinalizeHelper;
	struct TempColorData { uchar r, g, b, a; };
	friend class palette_data;

	[[nodiscard]] static auto PrepareFinalizeHelper(const colorizer &with)
		-> std::unique_ptr<FinalizeHelper>;

	void clear();
	void compute(int index, int fallbackIndex, TempColorData value);
	void setData(int index, const internal::ColorData &value);

	std::unique_ptr<FinalizeHelper> _finalizeHelper;
	bool _ready = false;

};

namespace main_palette {

not_null<const palette*> get();
QByteArray save();
bool load(const QByteArray &cache);
palette::SetResult setColor(QLatin1String name, uchar r, uchar g, uchar b, uchar a);
palette::SetResult setColor(QLatin1String name, QLatin1String from);
void apply(const palette &other);
void reset();
void reset(const colorizer &with);
int indexOfColor(color c);

} // namespace main_palette
} // namespace style
