// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flags.h"

// ANGLE is used only on Windows with Qt < 6.
#if defined Q_OS_WIN && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#define DESKTOP_APP_USE_ANGLE
#endif // Q_OS_WIN && Qt < 6

class QOpenGLContext;

namespace Ui::GL {

enum class Backend {
	Raster,
	OpenGL,
};

struct Capabilities {
	bool supported = false;
	bool transparency = false;
};

[[nodiscard]] Capabilities CheckCapabilities(QWidget *widget = nullptr);
[[nodiscard]] Backend ChooseBackendDefault(Capabilities capabilities);

void ForceDisable(bool disable);

void DetectLastCheckCrash();
[[nodiscard]] bool LastCrashCheckFailed();
void CrashCheckFinish();

#ifdef DESKTOP_APP_USE_ANGLE
enum class ANGLE {
	Auto,
	D3D9,
	D3D11,
	D3D11on12,
	//OpenGL,
};

void ConfigureANGLE(); // Requires Ui::Integration being set.
void ChangeANGLE(ANGLE backend);
[[nodiscard]] ANGLE CurrentANGLE();
#endif // DESKTOP_APP_USE_ANGLE

} // namespace Ui::GL
