// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_detection.h"

#include "ui/gl/gl_shader.h"
#include "ui/integration.h"
#include "base/debug_log.h"
#include "base/platform/base_platform_info.h"

#include <QtCore/QSet>
#include <QtCore/QFile>
#include <QtGui/QWindow>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QOpenGLWidget>

#ifdef DESKTOP_APP_USE_ANGLE
#include <QtGui/QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#include <EGL/egl.h>
#endif // DESKTOP_APP_USE_ANGLE

#define LOG_ONCE(x) [[maybe_unused]] static auto logged = [&] { LOG(x); return true; }();

namespace Ui::GL {
namespace {

bool ForceDisabled/* = false*/;
bool LastCheckCrashed/* = false*/;

#ifdef DESKTOP_APP_USE_ANGLE
ANGLE ResolvedANGLE/* = ANGLE::Auto*/;

QList<QByteArray> EGLExtensions(not_null<QOpenGLContext*> context) {
	const auto native = QGuiApplication::platformNativeInterface();
	Assert(native != nullptr);

	const auto display = static_cast<EGLDisplay>(
		native->nativeResourceForContext(
			QByteArrayLiteral("egldisplay"),
			context));
	return display
		? QByteArray(eglQueryString(display, EGL_EXTENSIONS)).split(' ')
		: QList<QByteArray>();
}
#endif // DESKTOP_APP_USE_ANGLE

void CrashCheckStart() {
	auto f = QFile(Integration::Instance().openglCheckFilePath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write("1", 1);
		f.close();
	}
}

} // namespace

Capabilities CheckCapabilities(QWidget *widget) {
	if (!Platform::IsMac()) {
		if (ForceDisabled) {
			LOG_ONCE(("OpenGL: Force-disabled."));
			return {};
		} else if (LastCheckCrashed) {
			LOG_ONCE(("OpenGL: Last-crashed."));
			return {};
		}
	}

	[[maybe_unused]] static const auto BugListInited = [] {
		if (!QFile::exists(":/misc/gpu_driver_bug_list.json")) {
			return false;
		}
		LOG(("OpenGL: Using custom 'gpu_driver_bug_list.json'."));
		qputenv("QT_OPENGL_BUGLIST", ":/misc/gpu_driver_bug_list.json");
		return true;
	}();

	CrashCheckStart();
	const auto guard = gsl::finally([=] {
		CrashCheckFinish();
	});

	auto tester = QOpenGLWidget(widget);
	if (!tester.window()->windowHandle()) {
		tester.window()->createWinId();
	}
	if (!tester.window()->windowHandle()) {
		LOG(("OpenGL: Could not create window for widget."));
		return {};
	}
	auto format = tester.window()->windowHandle()->format();
	format.setAlphaBufferSize(8);
	tester.window()->windowHandle()->setFormat(format);
	tester.setFormat(format);
	tester.grabFramebuffer(); // Force initialize().

	const auto context = tester.context();
	if (!context
		|| !context->isValid()
		|| !context->makeCurrent(tester.window()->windowHandle())) {
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
	{
		auto program = QOpenGLShaderProgram();
		LinkProgram(
			&program,
			VertexShader({
				VertexViewportTransform(),
				VertexPassTextureCoord(),
			}),
			FragmentShader({
				FragmentSampleARGB32Texture(),
			}));
		if (!program.isLinked()) {
			LOG_ONCE(("OpenGL: Could not link simple shader."));
			return {};
		}
	}

	const auto supported = context->format();
	switch (supported.profile()) {
	case QSurfaceFormat::NoProfile: {
		if (supported.renderableType() == QSurfaceFormat::OpenGLES) {
			LOG_ONCE(("OpenGL Profile: OpenGLES."));
		} else {
			LOG_ONCE(("OpenGL Profile: NoProfile."));
		}
	} break;
	case QSurfaceFormat::CoreProfile: {
		LOG_ONCE(("OpenGL Profile: Core."));
	} break;
	case QSurfaceFormat::CompatibilityProfile: {
		LOG_ONCE(("OpenGL Profile: Compatibility."));
	} break;
	}

	static const auto checkVendor = [&] {
		const auto renderer = reinterpret_cast<const char*>(
			functions->glGetString(GL_RENDERER));
		LOG(("OpenGL Renderer: %1").arg(renderer ? renderer : "[nullptr]"));
		const auto vendor = reinterpret_cast<const char*>(
			functions->glGetString(GL_VENDOR));
		LOG(("OpenGL Vendor: %1").arg(vendor ? vendor : "[nullptr]"));
		const auto version = reinterpret_cast<const char*>(
			functions->glGetString(GL_VERSION));
		LOG(("OpenGL Version: %1").arg(version ? version : "[nullptr]"));
		const auto extensions = context->extensions();
		auto list = QStringList();
		for (const auto &extension : extensions) {
			list.append(QString::fromLatin1(extension));
		}
		LOG(("OpenGL Extensions: %1").arg(list.join(", ")));

#ifdef DESKTOP_APP_USE_ANGLE
		auto egllist = QStringList();
		for (const auto &extension : EGLExtensions(context)) {
			egllist.append(QString::fromLatin1(extension));
		}
		LOG(("EGL Extensions: %1").arg(egllist.join(", ")));
#endif // DESKTOP_APP_USE_ANGLE

		return true;
	}();
	if (!checkVendor) {
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

Backend ChooseBackendDefault(Capabilities capabilities) {
	const auto use = ::Platform::IsMac()
		? true
		: ::Platform::IsWindows()
		? capabilities.supported
		: capabilities.transparency;
	return use ? Backend::OpenGL : Backend::Raster;
}

void DetectLastCheckCrash() {
	[[maybe_unused]] static const auto Once = [] {
		LastCheckCrashed = !Platform::IsMac()
			&& QFile::exists(Integration::Instance().openglCheckFilePath());
		return false;
	}();
}

bool LastCrashCheckFailed() {
	DetectLastCheckCrash();
	return LastCheckCrashed;
}

void CrashCheckFinish() {
	QFile::remove(Integration::Instance().openglCheckFilePath());
}

void ForceDisable(bool disable) {
	if (!Platform::IsMac()) {
		ForceDisabled = disable;
	}
}

#ifdef DESKTOP_APP_USE_ANGLE
void ConfigureANGLE() {
	qunsetenv("DESKTOP_APP_QT_ANGLE_PLATFORM");
	const auto path = Ui::Integration::Instance().angleBackendFilePath();
	if (path.isEmpty()) {
		return;
	}
	auto f = QFile(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return;
	}
	auto bytes = f.read(32);
	const auto check = [&](const char *backend, ANGLE angle) {
		if (bytes.startsWith(backend)) {
			ResolvedANGLE = angle;
			qputenv("DESKTOP_APP_QT_ANGLE_PLATFORM", backend);
		}
	};
	//check("gl", ANGLE::OpenGL);
	check("d3d9", ANGLE::D3D9);
	check("d3d11", ANGLE::D3D11);
	check("d3d11on12", ANGLE::D3D11on12);
	if (ResolvedANGLE == ANGLE::Auto) {
		LOG(("ANGLE Warning: Unknown backend: %1"
			).arg(QString::fromUtf8(bytes)));
	}
}

void ChangeANGLE(ANGLE backend) {
	const auto path = Ui::Integration::Instance().angleBackendFilePath();
	const auto write = [&](QByteArray backend) {
		auto f = QFile(path);
		if (!f.open(QIODevice::WriteOnly)) {
			LOG(("ANGLE Warning: Could not write to %1.").arg(path));
			return;
		}
		f.write(backend);
	};
	switch (backend) {
	case ANGLE::Auto: QFile(path).remove(); break;
	case ANGLE::D3D9: write("d3d9"); break;
	case ANGLE::D3D11: write("d3d11"); break;
	case ANGLE::D3D11on12: write("d3d11on12"); break;
	//case ANGLE::OpenGL: write("gl"); break;
	default: Unexpected("ANGLE backend value.");
	}
}

ANGLE CurrentANGLE() {
	return ResolvedANGLE;
}
#endif // DESKTOP_APP_USE_ANGLE

} // namespace Ui::GL
