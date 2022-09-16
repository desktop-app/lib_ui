// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/effects/spoiler_mess.h"

class SpoilerClickHandler;

namespace Ui::Text {

struct SpoilerData {
	explicit SpoilerData(Fn<void()> repaint)
	: animation(std::move(repaint)) {
	}

	SpoilerAnimation animation;
	QVector<std::shared_ptr<SpoilerClickHandler>> links;
};

} // namespace Ui::Text
