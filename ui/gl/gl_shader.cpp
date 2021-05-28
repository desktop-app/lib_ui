// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/gl/gl_shader.h"

#include "base/debug_log.h"

namespace Ui::GL {

QString VertexShader(const std::vector<ShaderPart> &parts) {
	const auto accumulate = [&](auto proj) {
		return ranges::accumulate(parts, QString(), std::plus<>(), proj);
	};
	return R"(
#version 120
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
	const auto accumulate = [&](auto proj) {
		return ranges::accumulate(parts, QString(), std::plus<>(), proj);
	};
	return R"(
#version 120
)" + accumulate(&ShaderPart::header) + R"(
void main() {
	vec4 result = vec4(0., 0., 0., 0.);
)" + accumulate(&ShaderPart::body) + R"(
	gl_FragColor = result;
}
)";
}

ShaderPart VertexPassTextureCoord() {
	return {
		.header = R"(
attribute vec2 texcoord;
varying vec2 v_texcoord;
)",
		.body = R"(
	v_texcoord = texcoord;
)",
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
	result = vec4(result.b, result.g, result.r, result.a);
)",
	};
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
	float y = texture2D(y_texture, v_texcoord).r;
	float u = texture2D(u_texture, v_texcoord).r - 0.5;
	float v = texture2D(v_texture, v_texcoord).r - 0.5;
	result = vec4(y + 1.403 * v, y - 0.344 * u - 0.714 * v, y + 1.77 * u, 1);
)",
	};
}

ShaderPart VertexViewportTransform() {
	return {
		.header = R"(
uniform vec2 viewport;
vec4 transform(vec4 position) {
	return vec4(
		vec2(-1, -1) + 2 * position.xy / viewport,
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
	vec2 rectHalf = roundRect.zw / 2;
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
		LOG(("Shader Compilation Failed: %1, error %2."
			).arg(source
			).arg(result->log()));
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
