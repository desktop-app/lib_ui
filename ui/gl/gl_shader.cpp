// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_shader.h"

#include "ui/gl/gl_image.h"
#include "base/debug_log.h"

#include <QtGui/QOpenGLContext>

namespace Ui::GL {

[[nodiscard]] bool IsOpenGLES() {
	const auto current = QOpenGLContext::currentContext();
	Assert(current != nullptr);

	return (current->format().renderableType() == QSurfaceFormat::OpenGLES);
}

QString VertexShader(const std::vector<ShaderPart> &parts) {
	const auto version = IsOpenGLES()
		? QString("#version 100\nprecision highp float;\n")
		: QString("#version 120\n");
	const auto accumulate = [&](auto proj) {
		return ranges::accumulate(parts, QString(), std::plus<>(), proj);
	};
	return version + R"(
attribute vec2 position;
)" + accumulate(&ShaderPart::header) + R"(
void main() {
	vec4 result = vec4(position, 0., 1.);
)" + accumulate(&ShaderPart::body) + R"(
	gl_Position = result;
}
)";
}

QString FragmentShader(const std::vector<ShaderPart> &parts) {
	const auto version = IsOpenGLES()
		? QString("#version 100\nprecision highp float;\n")
		: QString("#version 120\n");
	const auto accumulate = [&](auto proj) {
		return ranges::accumulate(parts, QString(), std::plus<>(), proj);
	};
	return version + accumulate(&ShaderPart::header) + R"(
void main() {
	vec4 result = vec4(0., 0., 0., 0.);
)" + accumulate(&ShaderPart::body) + R"(
	gl_FragColor = result;
}
)";
}

ShaderPart VertexPassTextureCoord(char prefix) {
	const auto name = prefix + QString("_texcoord");
	return {
		.header = R"(
attribute vec2 )" + name + R"(In;
varying vec2 )" + name + ";\n",
		.body = R"(
	)" + name + " = " + name + "In;\n",
	};
}

ShaderPart FragmentSampleARGB32Texture() {
	return {
		.header = R"(
varying vec2 v_texcoord;
uniform sampler2D s_texture;
)",
		.body = R"(
	result = texture2D(s_texture, v_texcoord);
)" + (kSwizzleRedBlue
	? R"(
	result = vec4(result.b, result.g, result.r, result.a);
)" : QString()),
	};
}

QString FragmentYUV2RGB() {
	return R"(
	result = vec4(
		1.164 * y + 1.596 * v,
		1.164 * y - 0.392 * u - 0.813 * v,
		1.164 * y + 2.17 * u,
		1.);
)";
}

ShaderPart FragmentSampleYUV420Texture() {
	return {
		.header = R"(
varying vec2 v_texcoord;
uniform sampler2D y_texture;
uniform sampler2D u_texture;
uniform sampler2D v_texture;
)",
		.body = R"(
	float y = texture2D(y_texture, v_texcoord).a - 0.0625;
	float u = texture2D(u_texture, v_texcoord).a - 0.5;
	float v = texture2D(v_texture, v_texcoord).a - 0.5;
)" + FragmentYUV2RGB(),
	};
}

ShaderPart FragmentSampleNV12Texture() {
	return {
		.header = R"(
varying vec2 v_texcoord;
uniform sampler2D y_texture;
uniform sampler2D uv_texture;
)",
		.body = R"(
	float y = texture2D(y_texture, v_texcoord).a - 0.0625;
	vec2 uv = texture2D(uv_texture, v_texcoord).rg - vec2(0.5, 0.5);
	float u = uv.x;
	float v = uv.y;
)" + FragmentYUV2RGB(),
	};
}

ShaderPart FragmentGlobalOpacity() {
	return {
		.header = R"(
uniform float g_opacity;
)",
		.body = R"(
	result *= g_opacity;
)",
	};
}

ShaderPart VertexViewportTransform() {
	return {
		.header = R"(
uniform vec2 viewport;
vec4 transform(vec4 position) {
	return vec4(
		vec2(-1, -1) + 2. * position.xy / viewport,
		position.z,
		position.w);
}
)",
		.body = R"(
	result = transform(result);
)",
	};
}

ShaderPart FragmentRoundCorners() {
	return {
		.header = R"(
uniform vec4 roundRect;
uniform vec2 radiusOutline;
uniform vec4 roundBg;
uniform vec4 outlineFg;
vec2 roundedCorner() {
	vec2 rectHalf = roundRect.zw / 2.;
	vec2 rectCenter = roundRect.xy + rectHalf;
	vec2 fromRectCenter = abs(gl_FragCoord.xy - rectCenter);
	vec2 vectorRadius = radiusOutline.xx + vec2(0.5, 0.5);
	vec2 fromCenterWithRadius = fromRectCenter + vectorRadius;
	vec2 fromRoundingCenter = max(fromCenterWithRadius, rectHalf)
		- rectHalf;
	float rounded = length(fromRoundingCenter) - radiusOutline.x;
	float outline = rounded + radiusOutline.y;

	return vec2(
		1. - smoothstep(0., 1., rounded),
		1. - (smoothstep(0., 1., outline) * outlineFg.a));
}
)",
		.body = R"(
	vec2 roundOutline = roundedCorner();
	result = result * roundOutline.y
		+ vec4(outlineFg.rgb, 1) * (1. - roundOutline.y);
	result = result * roundOutline.x + roundBg * (1. - roundOutline.x);
)",
	};
}

ShaderPart FragmentStaticColor() {
	return {
		.header = R"(
uniform vec4 s_color;
)",
		.body = R"(
	result = s_color;
)",
	};
}

not_null<QOpenGLShader*> MakeShader(
		not_null<QOpenGLShaderProgram*> program,
		QOpenGLShader::ShaderType type,
		const QString &source) {
	const auto result = new QOpenGLShader(type, program);
	if (!result->compileSourceCode(source)) {
		LOG(("Shader Compilation Failed: %1, error %2.").arg(
			source,
			result->log()));
	}
	program->addShader(result);
	return result;
}

Program LinkProgram(
		not_null<QOpenGLShaderProgram*> program,
		std::variant<QString, not_null<QOpenGLShader*>> vertex,
		std::variant<QString, not_null<QOpenGLShader*>> fragment) {
	const auto vertexAsSource = v::is<QString>(vertex);
	const auto v = vertexAsSource
		? MakeShader(
			program,
			QOpenGLShader::Vertex,
			v::get<QString>(vertex))
		: v::get<not_null<QOpenGLShader*>>(vertex);
	if (!vertexAsSource) {
		program->addShader(v);
	}
	const auto fragmentAsSource = v::is<QString>(fragment);
	const auto f = fragmentAsSource
		? MakeShader(
			program,
			QOpenGLShader::Fragment,
			v::get<QString>(fragment))
		: v::get<not_null<QOpenGLShader*>>(fragment);
	if (!fragmentAsSource) {
		program->addShader(f);
	}
	if (!program->link()) {
		LOG(("Shader Link Failed: %1.").arg(program->log()));
	}
	return { v, f };
}

} // namespace Ui::GL
