// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_variant.h"

namespace v::text {

rpl::producer<QString> take_plain(
		data &&d,
		rpl::producer<QString> &&fallback) {
	using RplMarked = rpl::producer<TextWithEntities>;
	if (const auto empty = std::get_if<v::null_t>(&d)) {
		return std::move(fallback);
	} else if (const auto ptr = std::get_if<QString>(&d)) {
		return rpl::single(base::take(*ptr));
	} else if (const auto ptr = std::get_if<rpl::producer<QString>>(&d)) {
		return base::take(*ptr);
	} else if (const auto ptr = std::get_if<TextWithEntities>(&d)) {
		return rpl::single(base::take(*ptr).text);
	} else if (const auto ptr = std::get_if<RplMarked>(&d)) {
		return base::take(*ptr) | rpl::map([](const auto &marked) {
			return marked.text;
		});
	}
	Unexpected("Bad variant in take_plain.");
}

rpl::producer<TextWithEntities> take_marked(
		data &&d,
		rpl::producer<TextWithEntities> &&fallback) {
	using RplMarked = rpl::producer<TextWithEntities>;
	if (const auto empty = std::get_if<v::null_t>(&d)) {
		return std::move(fallback);
	} else if (const auto ptr = std::get_if<QString>(&d)) {
		return rpl::single(TextWithEntities{ base::take(*ptr) });
	} else if (const auto ptr = std::get_if<rpl::producer<QString>>(&d)) {
		return base::take(*ptr) | rpl::map(TextWithEntities::Simple);
	} else if (const auto ptr = std::get_if<TextWithEntities>(&d)) {
		return rpl::single(base::take(*ptr));
	} else if (const auto ptr = std::get_if<RplMarked>(&d)) {
		return base::take(*ptr);
	}
	Unexpected("Bad variant in take_marked.");
}

} // namespace v::text

