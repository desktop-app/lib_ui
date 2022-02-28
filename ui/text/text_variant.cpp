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
	if (const auto empty = std::get_if<0>(&d)) {
		return std::move(fallback);
	} else if (const auto plainPtr = std::get_if<1>(&d)) {
		return rpl::single(base::take(*plainPtr));
	} else if (const auto rplPlainPtr = std::get_if<2>(&d)) {
		return base::take(*rplPlainPtr);
	} else if (const auto markedPtr = std::get_if<3>(&d)) {
		return rpl::single(base::take(*markedPtr).text);
	} else if (const auto rplMarkedPtr = std::get_if<4>(&d)) {
		return base::take(*rplMarkedPtr) | rpl::map([](const auto &marked) {
			return marked.text;
		});
	}
	Unexpected("Bad variant in take_plain.");
}

rpl::producer<TextWithEntities> take_marked(
		data &&d,
		rpl::producer<TextWithEntities> &&fallback) {
	if (const auto empty = std::get_if<0>(&d)) {
		return std::move(fallback);
	} else if (const auto plainPtr = std::get_if<1>(&d)) {
		return rpl::single(TextWithEntities{ base::take(*plainPtr) });
	} else if (const auto rplPlainPtr = std::get_if<2>(&d)) {
		return base::take(*rplPlainPtr) | rpl::map(TextWithEntities::Simple);
	} else if (const auto markedPtr = std::get_if<3>(&d)) {
		return rpl::single(base::take(*markedPtr));
	} else if (const auto rplMarkedPtr = std::get_if<4>(&d)) {
		return base::take(*rplMarkedPtr);
	}
	Unexpected("Bad variant in take_marked.");
}

} // namespace v::text

