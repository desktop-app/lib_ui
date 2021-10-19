// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtCore/QString>
#include <QOpenGLShader>

class OpenGLShaderProgram;

namespace Ui::GL {

struct ShaderPart {
	QString header;
	QString body;
};

[[nodiscard]] QString VertexShader(const std::vector<ShaderPart> &parts);
[[nodiscard]] QString FragmentShader(const std::vector<ShaderPart> &parts);

[[nodiscard]] ShaderPart VertexPassTextureCoord(char prefix = 'v');
[[nodiscard]] ShaderPart FragmentSampleARGB32Texture();
[[nodiscard]] ShaderPart FragmentSampleYUV420Texture();
[[nodiscard]] ShaderPart FragmentGlobalOpacity();
[[nodiscard]] ShaderPart VertexViewportTransform();
[[nodiscard]] ShaderPart FragmentRoundCorners();
[[nodiscard]] ShaderPart FragmentStaticColor();

not_null<QOpenGLShader*> MakeShader(
	not_null<QOpenGLShaderProgram*> program,
	QOpenGLShader::ShaderType type,
	const QString &source);

struct Program {
	not_null<QOpenGLShader*> vertex;
	not_null<QOpenGLShader*> fragment;
};

Program LinkProgram(
	not_null<QOpenGLShaderProgram*> program,
	std::variant<QString, not_null<QOpenGLShader*>> vertex,
	std::variant<QString, not_null<QOpenGLShader*>> fragment);

} // namespace Ui::GL
