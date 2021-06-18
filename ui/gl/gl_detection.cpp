// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_detection.h"

#include "ui/integration.h"
#include "base/debug_log.h"

#include <QtCore/QSet>
#include <QtGui/QWindow>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtWidgets/QOpenGLWidget>

#define LOG_ONCE(x) static auto logged = [&] { LOG(x); return true; }();

namespace Ui::GL {
namespace {

bool ForceDisabled/* = false*/;

} // namespace

Capabilities CheckCapabilities(QWidget *widget) {
	if (ForceDisabled) {
		LOG_ONCE(("OpenGL: Force-disabled."));
		return {};
	}
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

	Ui::Integration::Instance().openglCheckStart();
	tester.grabFramebuffer(); // Force initialize().
	Ui::Integration::Instance().openglCheckFinish();

	if (!tester.window()->windowHandle()) {
		tester.window()->createWinId();
	}
	const auto context = tester.context();
	if (!context
		|| !context->isValid()/*
		// This check doesn't work for a widget with WA_NativeWindow.
		|| !context->makeCurrent(tester.window()->windowHandle())*/) {
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
	switch (supported.profile()) {
	case QSurfaceFormat::NoProfile: {
		if (supported.renderableType() == QSurfaceFormat::OpenGLES) {
			LOG_ONCE(("OpenGL Profile: OpenGLES."));
		} else {
			LOG_ONCE(("OpenGL Profile: None."));
			return {};
		}
	} break;
	case QSurfaceFormat::CoreProfile: {
		LOG_ONCE(("OpenGL Profile: Core."));
	} break;
	case QSurfaceFormat::CompatibilityProfile: {
		LOG_ONCE(("OpenGL Profile: Compatibility."));
	} break;
	}
	static const auto extensionsLogged = [&] {
		const auto renderer = reinterpret_cast<const char*>(
			functions->glGetString(GL_RENDERER));
		LOG(("OpenGL Renderer: %1").arg(renderer ? renderer : "[nullptr]"));
		const auto vendor = reinterpret_cast<const char*>(
			functions->glGetString(GL_VENDOR));
		LOG(("OpenGL Vendor: %1").arg(vendor ? vendor : "[nullptr]"));
		const auto version = reinterpret_cast<const char*>(
			functions->glGetString(GL_VERSION));
		LOG(("OpenGL Version: %1").arg(version ? version : "[nullptr]"));
		auto list = QStringList();
		for (const auto extension : context->extensions()) {
			list.append(QString::fromLatin1(extension));
		}
		LOG(("OpenGL Extensions: %1").arg(list.join(", ")));
		return true;
	}();
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

void ForceDisable(bool disable) {
	ForceDisabled = disable;
}

} // namespace Ui::GL
