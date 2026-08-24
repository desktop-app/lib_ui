// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <emmintrin.h>

#include <cstring>

namespace Images::StackBlur {

using Quad = __m128i;
using Divider = __m128i;

[[nodiscard]] TG_FORCE_INLINE Divider MakeDivider(int radius) {
	const auto step = radius + 1;
	return _mm_set1_epi32(int32((0x80000000ULL + step - 1) / step));
}

// SSE2 has no 32 bit multiply high, so take the even and the odd lanes apart.
[[nodiscard]] TG_FORCE_INLINE Quad MultiplyHigh(Quad value, Divider divider) {
	const auto even = _mm_srli_epi64(_mm_mul_epu32(value, divider), 31);
	const auto odd = _mm_srli_epi64(
		_mm_mul_epu32(_mm_srli_si128(value, 4), divider),
		31);
	return _mm_or_si128(even, _mm_slli_si128(odd, 4));
}

// Halving twice by radius + 1 is floor(value / divsum), exact up to radius 213.
[[nodiscard]] TG_FORCE_INLINE Quad Divide(Quad value, Divider divider) {
	return MultiplyHigh(MultiplyHigh(value, divider), divider);
}

[[nodiscard]] TG_FORCE_INLINE Quad Zero() {
	return _mm_setzero_si128();
}

[[nodiscard]] TG_FORCE_INLINE Quad Add(Quad a, Quad b) {
	return _mm_add_epi32(a, b);
}

[[nodiscard]] TG_FORCE_INLINE Quad Sub(Quad a, Quad b) {
	return _mm_sub_epi32(a, b);
}

[[nodiscard]] TG_FORCE_INLINE Quad Scale(Quad a, int by) {
	const auto multiplier = _mm_set1_epi32(by);
	const auto mask = _mm_set_epi32(0, -1, 0, -1);
	const auto even = _mm_and_si128(_mm_mul_epu32(a, multiplier), mask);
	const auto odd = _mm_and_si128(
		_mm_mul_epu32(_mm_srli_si128(a, 4), multiplier),
		mask);
	return _mm_or_si128(even, _mm_slli_si128(odd, 4));
}

[[nodiscard]] TG_FORCE_INLINE Quad Load(const uchar *pixel) {
	auto raw = uint32();
	memcpy(&raw, pixel, 4);
	const auto zero = _mm_setzero_si128();
	const auto bytes = _mm_cvtsi32_si128(int32(raw));
	return _mm_unpacklo_epi16(_mm_unpacklo_epi8(bytes, zero), zero);
}

[[nodiscard]] TG_FORCE_INLINE uint32 Pack(Quad value) {
	const auto packed = _mm_packus_epi16(
		_mm_packs_epi32(value, value),
		_mm_setzero_si128());
	return uint32(_mm_cvtsi128_si32(packed));
}

TG_FORCE_INLINE void Load4(const uchar *pixels, Quad *quads) {
	const auto raw = _mm_loadu_si128(
		reinterpret_cast<const __m128i*>(pixels));
	const auto zero = _mm_setzero_si128();
	const auto low = _mm_unpacklo_epi8(raw, zero);
	const auto high = _mm_unpackhi_epi8(raw, zero);
	quads[0] = _mm_unpacklo_epi16(low, zero);
	quads[1] = _mm_unpackhi_epi16(low, zero);
	quads[2] = _mm_unpacklo_epi16(high, zero);
	quads[3] = _mm_unpackhi_epi16(high, zero);
}

TG_FORCE_INLINE void Store4KeepAlpha(uchar *pixels, const Quad *quads) {
	const auto low = _mm_packs_epi32(quads[0], quads[1]);
	const auto high = _mm_packs_epi32(quads[2], quads[3]);
	const auto packed = _mm_packus_epi16(low, high);
	const auto at = reinterpret_cast<__m128i*>(pixels);
	const auto alpha = _mm_set1_epi32(int32(0xFF000000U));
	_mm_storeu_si128(at, _mm_or_si128(
		_mm_and_si128(alpha, _mm_loadu_si128(at)),
		_mm_andnot_si128(alpha, packed)));
}

} // namespace Images::StackBlur
