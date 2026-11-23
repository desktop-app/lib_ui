// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/fields/input_field.h"

namespace Ui {

bool InputField::ContentsChange::Compose(
		QStringView before,
		QStringView after,
		int &position,
		int &removed,
		int &added) {
	if (before == after) {
		return true;
	}
	const auto size = int(before.size());
	const auto delta = int(after.size()) - size;
	if (!removed
		&& (position + added <= size)
		&& (delta == -added)
		&& (after.left(position) == before.left(position))
		&& (after.mid(position) == before.mid(position + added))) {
		return false;
	}
	const auto limit = int(std::min(before.size(), after.size()));
	const auto prefix = int(std::mismatch(
		before.begin(),
		before.begin() + limit,
		after.begin()).first - before.begin());
	const auto suffix = int(std::mismatch(
		before.rbegin(),
		before.rbegin() + (limit - prefix),
		after.rbegin()).first - before.rbegin());
	auto from = prefix;
	auto till = size - suffix;
	if (removed || added) {
		from = std::min(from, position);
		till = std::max(till, position + added);
	}
	removed += till - from - added;
	added = till - from + delta;
	position = from;
	return true;
}

} // namespace Ui
