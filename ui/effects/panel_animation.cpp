// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/panel_animation.h"

#include "ui/effects/animation_value.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"

#include <QtGui/QPainter>

namespace Ui {

void RoundShadowAnimation::start(int frameWidth, int frameHeight, float64 devicePixelRatio) {
	Expects(!started());

	_frameWidth = frameWidth;
	_frameHeight = frameHeight;
	_frame = QImage(_frameWidth, _frameHeight, QImage::Format_ARGB32_Premultiplied);
	_frame.setDevicePixelRatio(devicePixelRatio);
	_frameIntsPerLine = (_frame.bytesPerLine() >> 2);
	_frameInts = reinterpret_cast<uint32*>(_frame.bits());
	_frameIntsPerLineAdded = _frameIntsPerLine - _frameWidth;
	Assert(_frame.depth() == static_cast<int>(sizeof(uint32) << 3));
	Assert(_frame.bytesPerLine() == (_frameIntsPerLine << 2));
	Assert(_frameIntsPerLineAdded >= 0);
}

void RoundShadowAnimation::setShadow(
		const style::BoxShadow &st,
		int cornerRadius) {
	auto boxShadow = BoxShadow(st);
	const auto grid = boxShadow.preparedGrid(cornerRadius);
	const auto ratio = style::DevicePixelRatio();
	_shadow.extend = boxShadow.extend() * ratio;
	_shadow.cache = grid.image;
	_shadow.cornerL = grid.cornerL;
	_shadow.cornerT = grid.cornerT;
	_shadow.cornerR = grid.cornerR;
	_shadow.cornerB = grid.cornerB;
	_shadow.middle = grid.middle;
	_shadow.opacity256 = qRound(boxShadow.opacity() * 256);
}

void RoundShadowAnimation::setCornerMasks(
		const std::array<QImage, 4> &corners) {
	setCornerMask(_topLeft, corners[0]);
	setCornerMask(_topRight, corners[1]);
	setCornerMask(_bottomLeft, corners[2]);
	setCornerMask(_bottomRight, corners[3]);
}

void RoundShadowAnimation::setCornerMask(Corner &corner, const QImage &image) {
	Expects(!started());

	corner.image = image;
	if (corner.valid()) {
		corner.width = corner.image.width();
		corner.height = corner.image.height();
		corner.bytes = corner.image.constBits();
		corner.bytesPerPixel = (corner.image.depth() >> 3);
		corner.bytesPerLineAdded = corner.image.bytesPerLine() - corner.width * corner.bytesPerPixel;
		Assert(corner.image.depth() == (corner.bytesPerPixel << 3));
		Assert(corner.bytesPerLineAdded >= 0);
	} else {
		corner.width = corner.height = 0;
	}
}

void RoundShadowAnimation::paintCorner(Corner &corner, int left, int top) {
	auto mask = corner.bytes;
	auto bytesPerPixel = corner.bytesPerPixel;
	auto bytesPerLineAdded = corner.bytesPerLineAdded;
	auto frameInts = _frameInts + top * _frameIntsPerLine + left;
	auto frameIntsPerLineAdd = _frameIntsPerLine - corner.width;
	for (auto y = 0; y != corner.height; ++y) {
		for (auto x = 0; x != corner.width; ++x) {
			auto alpha = static_cast<uint32>(*mask) + 1;
			*frameInts = anim::unshifted(anim::shifted(*frameInts) * alpha);
			++frameInts;
			mask += bytesPerPixel;
		}
		frameInts += frameIntsPerLineAdd;
		mask += bytesPerLineAdded;
	}
}

void RoundShadowAnimation::paintShadow(int left, int top, int right, int bottom) {
	if (!_shadow.valid()) {
		return;
	}

	const auto ratio = style::DevicePixelRatio();
	const auto cL = _shadow.cornerL * ratio;
	const auto cT = _shadow.cornerT * ratio;
	const auto cR = _shadow.cornerR * ratio;
	const auto cB = _shadow.cornerB * ratio;
	const auto mid = _shadow.middle * ratio;
	const auto srcR = cL + mid;
	const auto srcB = cT + mid;

	// Corners (1:1 from cache to frame, both in physical pixels).
	paintShadowTile(left, top, cL, cT, 0, 0, cL, cT);
	paintShadowTile(right - cR, top, cR, cT, srcR, 0, cR, cT);
	paintShadowTile(left, bottom - cB, cL, cB, 0, srcB, cL, cB);
	paintShadowTile(right - cR, bottom - cB, cR, cB, srcR, srcB, cR, cB);

	// Sides (stretched from the middle strip).
	paintShadowTile(left + cL, top, right - left - cL - cR, cT, cL, 0, mid, cT);
	paintShadowTile(left + cL, bottom - cB, right - left - cL - cR, cB, cL, srcB, mid, cB);
	paintShadowTile(left, top + cT, cL, bottom - top - cT - cB, 0, cT, cL, mid);
	paintShadowTile(right - cR, top + cT, cR, bottom - top - cT - cB, srcR, cT, cR, mid);
}

void RoundShadowAnimation::paintShadowTile(
		int dstX,
		int dstY,
		int dstW,
		int dstH,
		int srcX,
		int srcY,
		int srcW,
		int srcH) {
	if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) {
		return;
	}

	const auto imageInts = reinterpret_cast<const uint32*>(
		_shadow.cache.constBits());
	const auto imageIntsPerLine = (_shadow.cache.bytesPerLine() >> 2);

	// Clamp destination to frame bounds.
	auto sx0 = srcX, sy0 = srcY;
	if (dstX < 0) {
		sx0 += (-dstX * srcW) / dstW;
		srcW -= (-dstX * srcW) / dstW;
		dstW += dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		sy0 += (-dstY * srcH) / dstH;
		srcH -= (-dstY * srcH) / dstH;
		dstH += dstY;
		dstY = 0;
	}
	if (dstX + dstW > _frameWidth) {
		dstW = _frameWidth - dstX;
	}
	if (dstY + dstH > _frameHeight) {
		dstH = _frameHeight - dstY;
	}
	if (dstW <= 0 || dstH <= 0) {
		return;
	}

	const auto opacityScale = _shadow.opacity256;
	auto frameRow = _frameInts + dstY * _frameIntsPerLine + dstX;
	for (auto y = 0; y != dstH; ++y) {
		const auto srcRow = sy0 + (y * srcH) / dstH;
		const auto imageRow = imageInts + srcRow * imageIntsPerLine;
		for (auto x = 0; x != dstW; ++x) {
			const auto srcCol = sx0 + (x * srcW) / dstW;
			const auto shadowPixel = imageRow[srcCol];
			const auto source = frameRow[x];
			const auto shadowAlpha = (std::max(
				_frameAlpha - int(source >> 24),
				0) * opacityScale) >> 8;
			frameRow[x] = anim::unshifted(
				anim::shifted(source) * 256
				+ anim::shifted(shadowPixel) * shadowAlpha);
		}
		frameRow += _frameIntsPerLine;
	}
}

void PanelAnimation::setFinalImage(
		QImage &&finalImage,
		QRect inner,
		int cornerRadius) {
	Expects(!started());

	_cornerRadius = cornerRadius;

	const auto pixelRatio = style::DevicePixelRatio();
	_finalImage = PixmapFromImage(
		std::move(finalImage).convertToFormat(
			QImage::Format_ARGB32_Premultiplied));

	Assert(!_finalImage.isNull());
	_finalWidth = _finalImage.width();
	_finalHeight = _finalImage.height();
	Assert(!(_finalWidth % pixelRatio));
	Assert(!(_finalHeight % pixelRatio));
	_finalInnerLeft = inner.x();
	_finalInnerTop = inner.y();
	_finalInnerWidth = inner.width();
	_finalInnerHeight = inner.height();
	Assert(!(_finalInnerLeft % pixelRatio));
	Assert(!(_finalInnerTop % pixelRatio));
	Assert(!(_finalInnerWidth % pixelRatio));
	Assert(!(_finalInnerHeight % pixelRatio));
	_finalInnerRight = _finalInnerLeft + _finalInnerWidth;
	_finalInnerBottom = _finalInnerTop + _finalInnerHeight;
	Assert(QRect(0, 0, _finalWidth, _finalHeight).contains(inner));

	setStartWidth();
	setStartHeight();
	setStartAlpha();
	setStartFadeTop();
	createFadeMask();
	setWidthDuration();
	setHeightDuration();
	setAlphaDuration();
	if (!_skipShadow) {
		setShadow(_st.shadow, _cornerRadius);
	}

	auto checkCorner = [this, inner](Corner &corner) {
		if (!corner.valid()) return;
		if ((_startWidth >= 0 && _startWidth < _finalWidth)
			|| (_startHeight >= 0 && _startHeight < _finalHeight)) {
			Assert(corner.width <= inner.width());
			Assert(corner.height <= inner.height());
		}
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
}

void PanelAnimation::setStartWidth() {
	_startWidth = qRound(_st.startWidth * _finalInnerWidth);
	if (_startWidth >= 0) Assert(_startWidth <= _finalInnerWidth);
}

void PanelAnimation::setStartHeight() {
	_startHeight = qRound(_st.startHeight * _finalInnerHeight);
	if (_startHeight >= 0) Assert(_startHeight <= _finalInnerHeight);
}

void PanelAnimation::setStartAlpha() {
	_startAlpha = qRound(_st.startOpacity * 255);
	Assert(_startAlpha >= 0 && _startAlpha < 256);
}

void PanelAnimation::setStartFadeTop() {
	_startFadeTop = qRound(_st.startFadeTop * _finalInnerHeight);
}

void PanelAnimation::createFadeMask() {
	auto resultHeight = qRound(_finalImage.height() * _st.fadeHeight);
	if (auto remove = (resultHeight % style::DevicePixelRatio())) {
		resultHeight -= remove;
	}
	auto finalAlpha = qRound(_st.fadeOpacity * 255);
	Assert(finalAlpha >= 0 && finalAlpha < 256);
	auto result = QImage(style::DevicePixelRatio(), resultHeight, QImage::Format_ARGB32_Premultiplied);
	auto ints = reinterpret_cast<uint32*>(result.bits());
	auto intsPerLineAdded = (result.bytesPerLine() >> 2) - style::DevicePixelRatio();
	auto up = (_origin == PanelAnimation::Origin::BottomLeft || _origin == PanelAnimation::Origin::BottomRight);
	auto from = up ? resultHeight : 0, to = resultHeight - from, delta = up ? -1 : 1;
	auto fadeFirstAlpha = up ? (finalAlpha + 1) : 1;
	auto fadeLastAlpha = up ? 1 : (finalAlpha + 1);
	_fadeFirst = QBrush(QColor(_st.fadeBg->c.red(), _st.fadeBg->c.green(), _st.fadeBg->c.blue(), (_st.fadeBg->c.alpha() * fadeFirstAlpha) >> 8));
	_fadeLast = QBrush(QColor(_st.fadeBg->c.red(), _st.fadeBg->c.green(), _st.fadeBg->c.blue(), (_st.fadeBg->c.alpha() * fadeLastAlpha) >> 8));
	for (auto y = from; y != to; y += delta) {
		auto alpha = static_cast<uint32>(finalAlpha * y) / resultHeight;
		auto value = (0xFFU << 24) | (alpha << 16) | (alpha << 8) | alpha;
		for (auto x = 0; x != style::DevicePixelRatio(); ++x) {
			*ints++ = value;
		}
		ints += intsPerLineAdded;
	}
	_fadeMask = PixmapFromImage(style::colorizeImage(result, _st.fadeBg));
	_fadeHeight = _fadeMask.height();
}

void PanelAnimation::setSkipShadow(bool skipShadow) {
	Assert(!started());
	_skipShadow = skipShadow;
}

void PanelAnimation::setWidthDuration() {
	_widthDuration = _st.widthDuration;
	Assert(_widthDuration >= 0.);
	Assert(_widthDuration <= 1.);
}

void PanelAnimation::setHeightDuration() {
	Assert(!started());
	_heightDuration = _st.heightDuration;
	Assert(_heightDuration >= 0.);
	Assert(_heightDuration <= 1.);
}

void PanelAnimation::setAlphaDuration() {
	Assert(!started());
	_alphaDuration = _st.opacityDuration;
	Assert(_alphaDuration >= 0.);
	Assert(_alphaDuration <= 1.);
}

void PanelAnimation::start() {
	Assert(!_finalImage.isNull());
	RoundShadowAnimation::start(_finalWidth, _finalHeight, _finalImage.devicePixelRatio());
	auto checkCorner = [this](const Corner &corner) {
		if (!corner.valid()) return;
		if (_startWidth >= 0) Assert(corner.width <= _startWidth);
		if (_startHeight >= 0) Assert(corner.height <= _startHeight);
		Assert(corner.width <= _finalInnerWidth);
		Assert(corner.height <= _finalInnerHeight);
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
}

auto PanelAnimation::computeState(float64 dt, float64 opacity) const
-> PaintState {
	auto &transition = anim::easeOutCirc;
	if (dt < _alphaDuration) {
		opacity *= transition(1., dt / _alphaDuration);
	}
	const auto widthProgress = (_startWidth < 0 || dt >= _widthDuration)
		? 1.
		: transition(1., dt / _widthDuration);
	const auto heightProgress = (_startHeight < 0 || dt >= _heightDuration)
		? 1.
		: transition(1., dt / _heightDuration);
	const auto frameWidth = (widthProgress < 1.)
		? anim::interpolate(_startWidth, _finalInnerWidth, widthProgress)
		: _finalInnerWidth;
	const auto frameHeight = (heightProgress < 1.)
		? anim::interpolate(_startHeight, _finalInnerHeight, heightProgress)
		: _finalInnerHeight;
	const auto ratio = style::DevicePixelRatio();
	return {
		.opacity = opacity,
		.widthProgress = widthProgress,
		.heightProgress = heightProgress,
		.fade = transition(1., dt),
		.width = frameWidth / ratio,
		.height = frameHeight / ratio,
	};
}

auto PanelAnimation::paintFrame(
	QPainter &p,
	int x,
	int y,
	int outerWidth,
	float64 dt,
	float64 opacity)
-> PaintState {
	Assert(started());
	Assert(dt >= 0.);

	const auto pixelRatio = style::DevicePixelRatio();

	const auto state = computeState(dt, opacity);
	opacity = state.opacity;
	_frameAlpha = anim::interpolate(1, 256, opacity);
	const auto frameWidth = state.width * pixelRatio;
	const auto frameHeight = state.height * pixelRatio;
	auto frameLeft = (_origin == Origin::TopLeft || _origin == Origin::BottomLeft) ? _finalInnerLeft : (_finalInnerRight - frameWidth);
	auto frameTop = (_origin == Origin::TopLeft || _origin == Origin::TopRight) ? _finalInnerTop : (_finalInnerBottom - frameHeight);
	auto frameRight = frameLeft + frameWidth;
	auto frameBottom = frameTop + frameHeight;

	auto fadeTop = (_fadeHeight > 0) ? std::clamp(anim::interpolate(_startFadeTop, _finalInnerHeight, state.fade), 0, frameHeight) : frameHeight;
	if (auto decrease = (fadeTop % pixelRatio)) {
		fadeTop -= decrease;
	}
	auto fadeBottom = (fadeTop < frameHeight) ? std::min(fadeTop + _fadeHeight, frameHeight) : frameHeight;
	auto fadeSkipLines = 0;
	if (_origin == Origin::BottomLeft || _origin == Origin::BottomRight) {
		fadeTop = frameHeight - fadeTop;
		fadeBottom = frameHeight - fadeBottom;
		qSwap(fadeTop, fadeBottom);
		fadeSkipLines = fadeTop + _fadeHeight - fadeBottom;
	}
	fadeTop += frameTop;
	fadeBottom += frameTop;

	if (opacity < 1.) {
		_frame.fill(Qt::transparent);
	}
	{
		QPainter p(&_frame);
		p.setOpacity(opacity);
		auto painterFrameLeft = frameLeft / pixelRatio;
		auto painterFrameTop = frameTop / pixelRatio;
		auto painterFadeBottom = fadeBottom / pixelRatio;
		p.drawPixmap(painterFrameLeft, painterFrameTop, _finalImage, frameLeft, frameTop, frameWidth, frameHeight);
		if (_fadeHeight) {
			if (frameTop != fadeTop) {
				p.fillRect(painterFrameLeft, painterFrameTop, frameWidth, fadeTop - frameTop, _fadeFirst);
			}
			if (fadeTop != fadeBottom) {
				auto painterFadeTop = fadeTop / pixelRatio;
				auto painterFrameWidth = frameWidth / pixelRatio;
				p.drawPixmap(painterFrameLeft, painterFadeTop, painterFrameWidth, painterFadeBottom - painterFadeTop, _fadeMask, 0, fadeSkipLines, pixelRatio, fadeBottom - fadeTop);
			}
			if (fadeBottom != frameBottom) {
				p.fillRect(painterFrameLeft, painterFadeBottom, frameWidth, frameBottom - fadeBottom, _fadeLast);
			}
		}
	}

	// Draw corners
	paintCorner(_topLeft, frameLeft, frameTop);
	paintCorner(_topRight, frameRight - _topRight.width, frameTop);
	paintCorner(_bottomLeft, frameLeft, frameBottom - _bottomLeft.height);
	paintCorner(_bottomRight, frameRight - _bottomRight.width, frameBottom - _bottomRight.height);

	// Draw shadow upon the transparent
	auto outerLeft = frameLeft;
	auto outerTop = frameTop;
	auto outerRight = frameRight;
	auto outerBottom = frameBottom;
	if (_shadow.valid()) {
		outerLeft -= _shadow.extend.left();
		outerTop -= _shadow.extend.top();
		outerRight += _shadow.extend.right();
		outerBottom += _shadow.extend.bottom();
	}
	if (pixelRatio > 1) {
		if (auto skipLeft = (outerLeft % pixelRatio)) {
			outerLeft -= skipLeft;
		}
		if (auto skipTop = (outerTop % pixelRatio)) {
			outerTop -= skipTop;
		}
		if (auto skipRight = (outerRight % pixelRatio)) {
			outerRight += (pixelRatio - skipRight);
		}
		if (auto skipBottom = (outerBottom % pixelRatio)) {
			outerBottom += (pixelRatio - skipBottom);
		}
	}

	if (opacity == 1.) {
		// Fill above the frame top with transparent.
		auto fillTopInts = (_frameInts + outerTop * _frameIntsPerLine + outerLeft);
		auto fillWidth = (outerRight - outerLeft) * sizeof(uint32);
		for (auto fillTop = frameTop - outerTop; fillTop != 0; --fillTop) {
			memset(fillTopInts, 0, fillWidth);
			fillTopInts += _frameIntsPerLine;
		}

		// Fill to the left and to the right of the frame with transparent.
		auto fillLeft = (frameLeft - outerLeft) * sizeof(uint32);
		auto fillRight = (outerRight - frameRight) * sizeof(uint32);
		if (fillLeft || fillRight) {
			auto fillInts = _frameInts + frameTop * _frameIntsPerLine;
			for (auto y = frameTop; y != frameBottom; ++y) {
				memset(fillInts + outerLeft, 0, fillLeft);
				memset(fillInts + frameRight, 0, fillRight);
				fillInts += _frameIntsPerLine;
			}
		}

		// Fill below the frame bottom with transparent.
		auto fillBottomInts = (_frameInts + frameBottom * _frameIntsPerLine + outerLeft);
		for (auto fillBottom = outerBottom - frameBottom; fillBottom != 0; --fillBottom) {
			memset(fillBottomInts, 0, fillWidth);
			fillBottomInts += _frameIntsPerLine;
		}
	}

	if (_shadow.valid()) {
		paintShadow(outerLeft, outerTop, outerRight, outerBottom);
	}

	// Debug
	//frameInts = _frameInts;
	//auto pattern = anim::shifted((static_cast<uint32>(0xFF) << 24) | (static_cast<uint32>(0xFF) << 16) | (static_cast<uint32>(0xFF) << 8) | static_cast<uint32>(0xFF));
	//for (auto y = 0; y != _finalHeight; ++y) {
	//	for (auto x = 0; x != _finalWidth; ++x) {
	//		auto source = *frameInts;
	//		auto sourceAlpha = (source >> 24);
	//		*frameInts = anim::unshifted(anim::shifted(source) * 256 + pattern * (256 - sourceAlpha));
	//		++frameInts;
	//	}
	//	frameInts += _frameIntsPerLineAdded;
	//}

	p.drawImage(style::rtlpoint(x + (outerLeft / pixelRatio), y + (outerTop / pixelRatio), outerWidth), _frame, QRect(outerLeft, outerTop, outerRight - outerLeft, outerBottom - outerTop));

	return state;
}

} // namespace Ui
