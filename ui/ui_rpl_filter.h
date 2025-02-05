// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <rpl/producer.h>

namespace rpl {
namespace details {

class filter_size_helper final {
public:
	template <typename Value, typename Error, typename Generator>
	auto operator()(rpl::producer<
			Value,
			Error,
			Generator> &&initial) const {
		return rpl::make_producer<Value, Error>([
			initial = std::move(initial)
		](const auto &consumer) mutable {
			return std::move(initial).start(
				[consumer](auto &&value) {
					if (value.width() > 0 && value.height() > 0) {
						consumer.put_next_forward(
							std::forward<decltype(value)>(value));
					}
				}, [consumer](auto &&error) {
					consumer.put_error_forward(
						std::forward<decltype(error)>(error));
				}, [consumer] {
					consumer.put_done();
				});
		});
	}

};

} // namespace details

inline auto filter_size() -> details::filter_size_helper {
	return details::filter_size_helper();
}

} // namespace rpl
