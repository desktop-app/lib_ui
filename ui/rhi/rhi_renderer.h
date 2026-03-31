// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QColor>

class QRhi;
class QRhiRenderTarget;
class QRhiCommandBuffer;

namespace Ui::Rhi {

class Renderer {
public:
	virtual void initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	}

	virtual void render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) = 0;

	virtual void renderOffscreen(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	}
	virtual void renderOnscreen(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	}

	virtual void releaseResources() {
	}

	[[nodiscard]] virtual QColor rhiClearColor() {
		return Qt::transparent;
	}

	virtual ~Renderer() = default;
};

} // namespace Ui::Rhi
