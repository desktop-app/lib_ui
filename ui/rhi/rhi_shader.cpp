// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/rhi/rhi_shader.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "base/debug_log.h"

#include <QtCore/QFile>

namespace Ui::Rhi {

QShader ShaderFromFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("RHI Shader Error: Could not open %1").arg(path));
		return {};
	}
	const auto data = file.readAll();
	auto shader = QShader::fromSerialized(data);
	if (!shader.isValid()) {
		LOG(("RHI Shader Error: Failed to deserialize %1").arg(path));
	}
	return shader;
}

} // namespace Ui::Rhi

#endif // Qt >= 6.7
