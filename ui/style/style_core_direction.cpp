// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/style/style_core_direction.h"

namespace style {
namespace {

bool RightToLeftValue = false;

} // namespace

bool RightToLeft() {
	return RightToLeftValue;
}

void SetRightToLeft(bool rtl) {
	RightToLeftValue = rtl;
}

} // namespace style

namespace st {

QString wrap_rtl(const QString &text) {
	const auto wrapper = QChar(rtl() ? 0x200F : 0x200E);
	return wrapper + text + wrapper;
}

} // namespace st
