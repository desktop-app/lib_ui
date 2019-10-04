// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace ph {

struct now_t {
};

inline constexpr auto now = now_t();

struct I {
	QString operator()(const QString &value) const { return value; };
};

template <typename P>
using Result = decltype(std::declval<P>()(QString()));

struct phrase {
	phrase(const QString &initial);
	template <std::size_t Size>
	phrase(const char (&initial)[Size])
	: phrase(QString::fromUtf8(initial, Size - 1)) {
	}

	template <typename P = I, typename = Result<P>>
	Result<P> operator()(ph::now_t, P p = P()) const {
		return p(value.current());
	};
	template <typename P = I, typename = Result<P>>
	rpl::producer<Result<P>> operator()(P p = P()) const {
		return value.value() | rpl::map(p);
	};

	rpl::variable<QString> value;
};

now_t start_phrase_count();
now_t check_phrase_count(int count);

namespace details {

template <int Count>
using phrase_value_array = std::array<
	std::pair<not_null<phrase*>, rpl::producer<QString>>,
	Count>;

template <int Count>
void set_values(phrase_value_array<Count> &&data) {
	for (auto &[single, value] : data) {
		single->value = std::move(value);
	}
}

} // namespace details
} // namespace ph
