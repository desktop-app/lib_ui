// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

namespace Images::StackBlur {

// Four neighbouring rows or columns are blurred at once, four channels each.
constexpr auto kLanes = 4;

// Each implementation below defines same vocabulary over one Quad.

} // namespace Images::StackBlur

// Architecture dependent implementations.

#if defined(__aarch64__) || defined(_M_ARM64)
#include "ui/image/image_blur_simd_neon.h"
#elif defined(__SSE2__) \
	|| defined(_M_X64) \
	|| (defined(_M_IX86_FP) && _M_IX86_FP >= 2) // __aarch64__ || _M_ARM64
#include "ui/image/image_blur_simd_sse2.h"
#else // __aarch64__ || _M_ARM64 || __SSE2__ || _M_X64 || _M_IX86_FP >= 2
#include "ui/image/image_blur_simd_generic.h"
#endif // else for __aarch64__ || _M_ARM64 || __SSE2__ || _M_X64 || _M_IX86_FP
