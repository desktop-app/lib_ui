// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_detection.h"

#include "base/debug_log.h"

#include <QtGui/QWindow>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtWidgets/QOpenGLWidget>

#define LOG_ONCE(x) static auto logged = [&] { LOG(x); return true; };

namespace Ui::GL {

Capabilities CheckCapabilities(QWidget *widget) {
	auto format = QSurfaceFormat();
	format.setAlphaBufferSize(8);
	if (widget) {
		if (!widget->window()->windowHandle()) {
			widget->window()->createWinId();
		}
		if (!widget->window()->windowHandle()) {
			LOG(("OpenGL: Could not create window for widget."));
			return {};
		}
		if (!widget->window()->windowHandle()->supportsOpenGL()) {
			LOG_ONCE(("OpenGL: Not supported for window."));
			return {};
		}
	}
	auto tester = QOpenGLWidget(widget);
	tester.setFormat(format);
	tester.grabFramebuffer(); // Force initialize().
	const auto context = tester.context();
	if (!context) {
		LOG_ONCE(("OpenGL: Could not create widget in a window."));
		return {};
	}
	const auto functions = context->functions();
	using Feature = QOpenGLFunctions;
	if (!functions->hasOpenGLFeature(Feature::NPOTTextures)) {
		LOG_ONCE(("OpenGL: NPOT textures not supported."));
		return {};
	} else if (!functions->hasOpenGLFeature(Feature::Framebuffers)) {
		LOG_ONCE(("OpenGL: Framebuffers not supported."));
		return {};
	} else if (!functions->hasOpenGLFeature(Feature::Shaders)) {
		LOG_ONCE(("OpenGL: Shaders not supported."));
		return {};
	}
	const auto supported = context->format();
	if (supported.profile() == QSurfaceFormat::NoProfile) {
		LOG_ONCE(("OpenGL: NoProfile received in final format."));
		return {};
	}
	const auto version = u"%1.%2"_q
		.arg(supported.majorVersion())
		.arg(supported.majorVersion());
	auto result = Capabilities{ .supported = true };
	if (supported.alphaBufferSize() >= 8) {
		result.transparency = true;
		LOG_ONCE(("OpenGL: QOpenGLContext created, version: %1."
			).arg(version));
	} else {
		LOG_ONCE(("OpenGL: QOpenGLContext without alpha created, version: %1"
			).arg(version));
	}
	return result;
}

} // namespace Ui::GL
