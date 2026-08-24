// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Images::StackBlur {

// Plain fallback for targets with neither NEON nor SSE2 available.
struct Quad {
	uint32 lane[4];
};
using Divider = uint32;

[[nodiscard]] TG_FORCE_INLINE Divider MakeDivider(int radius) {
	const auto step = radius + 1;
	return uint32((0x80000000ULL + step - 1) / step);
}

// Halving twice by radius + 1 is floor(value / divsum), exact up to radius 213.
[[nodiscard]] TG_FORCE_INLINE Quad Divide(Quad value, Divider divider) {
	for (auto i = 0; i != 4; ++i) {
		const auto once = uint32((uint64(value.lane[i]) * divider) >> 31);
		value.lane[i] = uint32((uint64(once) * divider) >> 31);
	}
	return value;
}

[[nodiscard]] TG_FORCE_INLINE Quad Zero() {
	return Quad();
}

[[nodiscard]] TG_FORCE_INLINE Quad Add(Quad a, Quad b) {
	for (auto i = 0; i != 4; ++i) {
		a.lane[i] += b.lane[i];
	}
	return a;
}

[[nodiscard]] TG_FORCE_INLINE Quad Sub(Quad a, Quad b) {
	for (auto i = 0; i != 4; ++i) {
		a.lane[i] -= b.lane[i];
	}
	return a;
}

[[nodiscard]] TG_FORCE_INLINE Quad Scale(Quad a, int by) {
	for (auto i = 0; i != 4; ++i) {
		a.lane[i] *= uint32(by);
	}
	return a;
}

[[nodiscard]] TG_FORCE_INLINE Quad Load(const uchar *pixel) {
	return Quad{ { pixel[0], pixel[1], pixel[2], pixel[3] } };
}

[[nodiscard]] TG_FORCE_INLINE uint32 Pack(Quad value) {
	return uchar(value.lane[0])
		| (uint32(uchar(value.lane[1])) << 8)
		| (uint32(uchar(value.lane[2])) << 16)
		| (uint32(uchar(value.lane[3])) << 24);
}

TG_FORCE_INLINE void Load4(const uchar *pixels, Quad *quads) {
	for (auto i = 0; i != 4; ++i) {
		quads[i] = Load(pixels + i * 4);
	}
}

TG_FORCE_INLINE void Store4KeepAlpha(uchar *pixels, const Quad *quads) {
	for (auto i = 0; i != 4; ++i) {
		const auto packed = Pack(quads[i]);
		pixels[i * 4] = uchar(packed);
		pixels[i * 4 + 1] = uchar(packed >> 8);
		pixels[i * 4 + 2] = uchar(packed >> 16);
	}
}

} // namespace Images::StackBlur
