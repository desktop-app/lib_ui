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

#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
#include "base/platform/linux/base_linux_library.h"
#endif // !Q_OS_MAC && !Q_OS_WIN

#include <QtCore/QSet>
#include <QtCore/QFile>
#include <QtCore/QLibraryInfo>
#include <QtGui/QWindow>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QOpenGLWidget>

#ifdef DESKTOP_APP_USE_ANGLE
#include <QtGui/QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#include <EGL/egl.h>
#endif // DESKTOP_APP_USE_ANGLE

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <rhi/qrhi.h>
#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
#include <QtGui/QOffscreenSurface>
#include <QtGui/QSurfaceFormat>
#endif // !Q_OS_MAC && !Q_OS_WIN
#endif // Qt >= 6.7

#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
extern "C" {
void _libOpenGL_so_tramp_resolve_all(void) __attribute__((weak));
void _libEGL_so_tramp_resolve_all(void) __attribute__((weak));
void _libGLX_so_tramp_resolve_all(void) __attribute__((weak));
} // extern "C"
#endif // !Q_OS_MAC && !Q_OS_WIN

#define LOG_ONCE(x) [[maybe_unused]] static auto logged = [&] { LOG(x); return true; }();

namespace Ui::GL {
namespace {

bool ForceDisabled/* = false*/;
bool LastCheckCrashed/* = false*/;

base::options::toggle OptionUseQtRhi({
	.id = kOptionUseQtRhi,
	.name = "Use Qt RHI renderer",
	.defaultValue = true,
	.scope = [] {
		return (!Platform::IsWindows() || Platform::IsWindowsARM64())
			&& QLibraryInfo::version() >= QVersionNumber(6, 7);
	},
	.restartRequired = true,
});

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

[[nodiscard]] bool OpenGLLibraryAvailable() {
#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
	static const auto available = [] {
		const auto check = [](void (*tramp)(), const char *name) {
			return !tramp
				|| (base::Platform::LoadLibrary(name, RTLD_NODELETE)
					!= nullptr);
		};
		if (!check(_libOpenGL_so_tramp_resolve_all, "libOpenGL.so.0")) {
			return false;
		} else if (Platform::IsWayland()) {
			return check(_libEGL_so_tramp_resolve_all, "libEGL.so.1");
		} else if (Platform::IsX11()) {
			return check(_libGLX_so_tramp_resolve_all, "libGLX.so.0");
		}
		return true;
	}();
	return available;
#else // !Q_OS_MAC && !Q_OS_WIN
	return true;
#endif // Q_OS_MAC || Q_OS_WIN
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
[[nodiscard]] RhiCapabilities ProbeRhiCapabilities() {
#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
	auto offscreen = std::unique_ptr<QOffscreenSurface>();
#endif // !Q_OS_MAC && !Q_OS_WIN
	auto rhi = std::unique_ptr<QRhi>();
#ifdef Q_OS_MAC
	if (Platform::MetalSupported()) {
		auto params = QRhiMetalInitParams();
		rhi.reset(QRhi::create(QRhi::Metal, &params));
	}
#elif defined(Q_OS_WIN) // Q_OS_MAC
	auto params = QRhiD3D11InitParams();
	rhi.reset(QRhi::create(QRhi::D3D11, &params));
#else // Q_OS_MAC || Q_OS_WIN
	const auto tryCreate = [&](QSurfaceFormat format) {
		offscreen.reset(QRhiGles2InitParams::newFallbackSurface(format));
		if (!offscreen) {
			return;
		}
		auto params = QRhiGles2InitParams();
		params.format = format;
		params.fallbackSurface = offscreen.get();
		rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
	};
	// Compute needs a 4.3 core context, plain 3D rendering does not. Probe
	// the richer format first to detect compute, then fall back so a
	// render-only GPU still counts as supported.
	auto format = QSurfaceFormat::defaultFormat();
	format.setVersion(4, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	tryCreate(format);
	if (!rhi) {
		tryCreate(QSurfaceFormat::defaultFormat());
	}
#endif // Q_OS_MAC || Q_OS_WIN
	if (!rhi) {
		LOG(("RHI: Probe failed, no device."));
		return {};
	}
	const auto compute = rhi->isFeatureSupported(QRhi::Compute);
	LOG(("RHI: Probe backend=%1 device=%2 compute=%3."
		).arg(rhi->backendName()
		).arg(rhi->driverInfo().deviceName
		).arg(compute ? "yes" : "no"));
	return {
		.supported = true,
		.compute = compute,
	};
}
#endif // Qt >= 6.7

} // namespace

const char kOptionUseQtRhi[] = "use-qt-rhi";

Capabilities CheckCapabilities(QWidget *widget) {
	if (WidgetsRhiSupported()) {
		return {};
	}
	if (!Platform::IsMac()) {
		if (ForceDisabled) {
			LOG_ONCE(("OpenGL: Force-disabled."));
			return {};
		} else if (LastCheckCrashed) {
			LOG_ONCE(("OpenGL: Last-crashed."));
			return {};
		} else if (!OpenGLLibraryAvailable()) {
			LOG_ONCE(("OpenGL: Library not available."));
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
	tester.setAttribute(Qt::WA_TranslucentBackground);
	if (tester.window()->testAttribute(Qt::WA_TranslucentBackground)) {
		auto format = tester.format();
		format.setAlphaBufferSize(8);
		tester.setFormat(format);
	}
	const auto guard2 = [&]() -> std::optional<gsl::final_action<Fn<void()>>> {
		if (!tester.window()->windowHandle()) {
			tester.window()->createWinId();
			return gsl::finally(Fn<void()>([&] {
				tester.window()->windowHandle()->destroy();
				tester.window()->setAttribute(Qt::WA_OutsideWSRange, false);
			}));
		}
		return std::nullopt;
	}();
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
	if (WidgetsRhiSupported()) {
		return Backend::QRhi;
	}
	const auto use = ::Platform::IsMac()
		? true
		: ::Platform::IsWindows()
		? capabilities.supported
		: capabilities.transparency;
	return use ? Backend::OpenGL : Backend::Raster;
}

bool WidgetsRhiEnabled() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (!OptionUseQtRhi.value()) {
		return false;
	} else if (!Platform::IsMac()) {
		if (ForceDisabled
			|| LastCrashCheckFailed()
			|| !OpenGLLibraryAvailable()) {
			return false;
		}
	}
	return true;
#else
	return false;
#endif
}

bool WidgetsRhiSupported() {
	return WidgetsRhiEnabled() && CheckRhiCapabilities().supported;
}

RhiCapabilities CheckRhiCapabilities() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	static const auto result = [] {
		if (!Platform::IsMac()) {
			if (ForceDisabled
				|| LastCrashCheckFailed()
				|| !OpenGLLibraryAvailable()) {
				return RhiCapabilities();
			}
			CrashCheckStart();
		}
		const auto value = ProbeRhiCapabilities();
		if (!Platform::IsMac()) {
			CrashCheckFinish();
		}
		return value;
	}();
	return result;
#else
	return {};
#endif
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
