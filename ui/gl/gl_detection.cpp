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
#include "base/options.h"
#include "base/platform/base_platform_info.h"

#include <QtCore/QSet>
#include <QtCore/QFile>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QOpenGLWindow>
#include <QOpenGLWidget>

#ifdef Q_OS_WIN
#include <QtGui/QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#include <EGL/egl.h>
#endif // Q_OS_WIN

#define LOG_ONCE(x) [[maybe_unused]] static auto logged = [&] { LOG(x); return true; }();

namespace Ui::GL {
namespace {

bool ForceDisabled/* = false*/;
bool LastCheckCrashed/* = false*/;

#ifdef Q_OS_WIN
ANGLE ResolvedANGLE/* = ANGLE::Auto*/;
#endif // Q_OS_WIN

base::options::toggle AllowLinuxNvidiaOpenGL({
	.id = kOptionAllowLinuxNvidiaOpenGL,
	.name = "Allow OpenGL on the NVIDIA drivers (Linux)",
	.description = "Qt+OpenGL have problems on Linux with NVIDIA drivers.",
	.scope = base::options::linux,
	.restartRequired = true,
});

void CrashCheckStart() {
	auto f = QFile(Integration::Instance().openglCheckFilePath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write("1", 1);
		f.close();
	}
}

} // namespace

const char kOptionAllowLinuxNvidiaOpenGL[] = "allow-linux-nvidia-opengl";

Capabilities CheckCapabilities(QWidget *widget, bool avoidWidgetCreation) {
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

	auto format = QSurfaceFormat();
	if (widget) {
		if (!widget->window()->windowHandle()) {
			widget->window()->createWinId();
		}
		if (!widget->window()->windowHandle()) {
			LOG(("OpenGL: Could not create window for widget."));
			return {};
		}
		format = widget->window()->windowHandle()->format();
		format.setAlphaBufferSize(8);
		widget->window()->windowHandle()->setFormat(format);
	} else {
		format.setAlphaBufferSize(8);
	}

	CrashCheckStart();
	const auto tester = [&] {
		std::unique_ptr<QObject> result;
		if (avoidWidgetCreation) {
			const auto w = new QOpenGLWindow();
			auto e = QResizeEvent(QSize(), QSize());
			w->setFormat(format);
			w->create();
			static_cast<QObject*>(w)->event(&e); // Force initialize().
			w->grabFramebuffer(); // Force makeCurrent().
			result.reset(w);
		} else {
			const auto w = new QOpenGLWidget(widget);
			w->setFormat(format);
			w->grabFramebuffer(); // Force initialize().
			if (!w->window()->windowHandle()) {
				w->window()->createWinId();
			}
			result.reset(w);
		}
		return result;
	}();
	const auto testerWidget = avoidWidgetCreation
		? nullptr
		: static_cast<QOpenGLWidget*>(tester.get());
	const auto testerWindow = avoidWidgetCreation
		? static_cast<QOpenGLWindow*>(tester.get())
		: nullptr;
	/*const auto testerQWindow = avoidWidgetCreation
		? static_cast<QWindow*>(tester.get())
		: testerWidget->window()->windowHandle();*/
	CrashCheckFinish();

	const auto context = avoidWidgetCreation ? testerWindow->context() : testerWidget->context();
	if (!context
		|| !context->isValid()/*
		// This check doesn't work for a widget with WA_NativeWindow.
		|| !context->makeCurrent(testerQWindow)*/) {
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

#ifdef Q_OS_WIN
		auto egllist = QStringList();
		for (const auto &extension : EGLExtensions(context)) {
			egllist.append(QString::fromLatin1(extension));
		}
		LOG(("EGL Extensions: %1").arg(egllist.join(", ")));
#endif // Q_OS_WIN

		if (::Platform::IsLinux()
			&& version
			&& QByteArray(version).contains("NVIDIA")) {
			// https://github.com/telegramdesktop/tdesktop/issues/16830
			if (AllowLinuxNvidiaOpenGL.value()) {
				LOG_ONCE(("OpenGL: Allow on NVIDIA driver (experimental)."));
			} else {
				LOG_ONCE(("OpenGL: Disable on NVIDIA driver on Linux."));
				return false;
			}
		}

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

#ifdef Q_OS_WIN

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

#endif // Q_OS_WIN

} // namespace Ui::GL
