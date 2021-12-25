// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/click_handler.h"

enum class EntityType : uchar;

class SpoilerClickHandler : public ClickHandler {
public:
	SpoilerClickHandler() = default;

	TextEntity getTextEntity() const override;

	void onClick(ClickContext context) const override;

	[[nodiscard]] bool shown() const;
	void setShown(bool value);
	[[nodiscard]] crl::time startMs() const;
	void setStartMs(crl::time value);

private:
	bool _shown = false;
	crl::time _startMs = 0;

};
