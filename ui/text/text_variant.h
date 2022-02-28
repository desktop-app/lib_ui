// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text_entity.h"

namespace v::text {

using data = std::variant<
	v::null_t,
	QString,
	rpl::producer<QString>,
	TextWithEntities,
	rpl::producer<TextWithEntities>>;

[[nodiscard]] rpl::producer<QString> take_plain(
	data &&d,
	rpl::producer<QString> &&fallback = rpl::never<QString>());
[[nodiscard]] rpl::producer<TextWithEntities> take_marked(
	data &&d,
	rpl::producer<TextWithEntities> &&fallback
		= rpl::never<TextWithEntities>());

} // namespace v::text
