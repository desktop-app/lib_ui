// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/wrap/wrap.h"

namespace Ui {

template <typename Widget = RpWidget>
class PaddingWrap;

template <>
class PaddingWrap<RpWidget> : public Wrap<RpWidget> {
	using Parent = Wrap<RpWidget>;

public:
	PaddingWrap(
		QWidget *parent,
		object_ptr<RpWidget> &&child,
		const style::margins &padding);

	style::margins padding() const {
		return _padding;
	}
	void setPadding(const style::margins &padding);

	int naturalWidth() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void wrappedSizeUpdated(QSize size) override;

private:
	style::margins _padding;

};

template <typename Widget>
class PaddingWrap : public Wrap<Widget, PaddingWrap<RpWidget>> {
	using Parent = Wrap<Widget, PaddingWrap<RpWidget>>;

public:
	PaddingWrap(
		QWidget *parent,
		object_ptr<Widget> &&child,
		const style::margins &padding)
	: Parent(parent, std::move(child), padding) {
	}

};

template <typename Widget = RpWidget>
class CenterWrap;

template <>
class CenterWrap<RpWidget> : public Wrap<RpWidget> {
	using Parent = Wrap<RpWidget>;

public:
	CenterWrap(
		QWidget *parent,
		object_ptr<RpWidget> &&child);

	int naturalWidth() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void wrappedSizeUpdated(QSize size) override;

private:
	void updateWrappedPosition(int forWidth);

};

template <typename Widget>
class CenterWrap : public Wrap<Widget, CenterWrap<RpWidget>> {
	using Parent = Wrap<Widget, CenterWrap<RpWidget>>;

public:
	CenterWrap(
		QWidget *parent,
		object_ptr<Widget> &&child)
	: Parent(parent, std::move(child)) {
	}

};

class FixedHeightWidget : public RpWidget {
public:
	explicit FixedHeightWidget(QWidget *parent, int height = 0)
	: RpWidget(parent) {
		resize(width(), height);
	}

};

inline object_ptr<FixedHeightWidget> CreateSkipWidget(
		QWidget *parent,
		int skip) {
	return object_ptr<FixedHeightWidget>(
		parent,
		skip);
}

} // namespace Ui
