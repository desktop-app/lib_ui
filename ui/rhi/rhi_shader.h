// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include <rhi/qshader.h>

namespace Ui::Rhi {

[[nodiscard]] QShader ShaderFromFile(const QString &path);

} // namespace Ui::Rhi

#endif // Qt >= 6.7
