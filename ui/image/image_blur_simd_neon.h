// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <arm_neon.h>

#include <cstring>

namespace Images::StackBlur {

using Quad = uint32x4_t;
using Divider = int32x4_t;

[[nodiscard]] TG_FORCE_INLINE Divider MakeDivider(int radius) {
	const auto step = radius + 1;
	return vdupq_n_s32(int32((0x80000000ULL + step - 1) / step));
}

// Halving twice by radius + 1 is floor(value / divsum), exact up to radius 213.
[[nodiscard]] TG_FORCE_INLINE Quad Divide(Quad value, Divider divider) {
	const auto once = vqdmulhq_s32(vreinterpretq_s32_u32(value), divider);
	return vreinterpretq_u32_s32(vqdmulhq_s32(once, divider));
}

[[nodiscard]] TG_FORCE_INLINE Quad Zero() {
	return vdupq_n_u32(0);
}

[[nodiscard]] TG_FORCE_INLINE Quad Add(Quad a, Quad b) {
	return vaddq_u32(a, b);
}

[[nodiscard]] TG_FORCE_INLINE Quad Sub(Quad a, Quad b) {
	return vsubq_u32(a, b);
}

[[nodiscard]] TG_FORCE_INLINE Quad Scale(Quad a, int by) {
	return vmulq_u32(a, vdupq_n_u32(uint32(by)));
}

[[nodiscard]] TG_FORCE_INLINE Quad Load(const uchar *pixel) {
	auto raw = uint32();
	memcpy(&raw, pixel, 4);
	const auto bytes = vreinterpret_u8_u32(vdup_n_u32(raw));
	return vmovl_u16(vget_low_u16(vmovl_u8(bytes)));
}

[[nodiscard]] TG_FORCE_INLINE uint32 Pack(Quad value) {
	const auto narrow = vmovn_u16(
		vcombine_u16(vmovn_u32(value), vdup_n_u16(0)));
	return vget_lane_u32(vreinterpret_u32_u8(narrow), 0);
}

TG_FORCE_INLINE void Load4(const uchar *pixels, Quad *quads) {
	const auto raw = vld1q_u8(pixels);
	const auto low = vmovl_u8(vget_low_u8(raw));
	const auto high = vmovl_u8(vget_high_u8(raw));
	quads[0] = vmovl_u16(vget_low_u16(low));
	quads[1] = vmovl_high_u16(low);
	quads[2] = vmovl_u16(vget_low_u16(high));
	quads[3] = vmovl_high_u16(high);
}

TG_FORCE_INLINE void Store4KeepAlpha(uchar *pixels, const Quad *quads) {
	const auto low = vcombine_u16(vmovn_u32(quads[0]), vmovn_u32(quads[1]));
	const auto high = vcombine_u16(vmovn_u32(quads[2]), vmovn_u32(quads[3]));
	const auto packed = vcombine_u8(vmovn_u16(low), vmovn_u16(high));
	const auto alpha = vreinterpretq_u8_u32(vdupq_n_u32(0xFF000000U));
	vst1q_u8(pixels, vbslq_u8(alpha, vld1q_u8(pixels), packed));
}

} // namespace Images::StackBlur
