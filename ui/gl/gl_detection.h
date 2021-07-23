// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flags.h"

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

void ForceDisable(bool disable);

[[nodiscard]] bool LastCrashCheckFailed();
void CrashCheckFinish();

// Windows only.
enum class ANGLE {
	Auto,
	D3D9,
	D3D11,
	D3D11on12,
	OpenGL,
};

void ConfigureANGLE(); // Requires Ui::Integration being set.
void ChangeANGLE(ANGLE backend);
[[nodiscard]] ANGLE CurrentANGLE();

[[nodiscard]] QList<QByteArray> EGLExtensions(
	not_null<QOpenGLContext*> context);

} // namespace Ui::GL
