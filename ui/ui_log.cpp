// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/ui_log.h"

#include "ui/ui_integration.h"

namespace Ui {

void WriteLogEntry(const QString &message) {
	Integration::Instance().writeLogEntry(message);
}

} // namespace Ui
