// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/timer.h"
#include "base/object_ptr.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "ui/rp_widget.h"
#include "ui/rect_part.h"

namespace style {
struct Tooltip;
struct ImportantTooltip;
struct FlatLabel;
struct PopupMenu;
} // namespace style

namespace st {
extern const style::FlatLabel &defaultFlatLabel;
extern const style::PopupMenu &defaultPopupMenu;
} // namespace st

namespace Ui {

class FlatLabel;

class AbstractTooltipShower {
public:
	virtual QString tooltipText() const = 0;
	virtual QPoint tooltipPos() const = 0;
	virtual bool tooltipWindowActive() const = 0;
	virtual const style::Tooltip *tooltipSt() const;
	virtual ~AbstractTooltipShower();

};

class Tooltip : public RpWidget {
public:
	static void Show(int32 delay, const AbstractTooltipShower *shower);
	static void Hide();

protected:
	void paintEvent(QPaintEvent *e) override;
	void hideEvent(QHideEvent *e) override;

	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void performShow();

	Tooltip();
	~Tooltip();

	void popup(const QPoint &p, const QString &text, const style::Tooltip *st);

	friend class AbstractTooltipShower;
	const AbstractTooltipShower *_shower = nullptr;
	base::Timer _showTimer;

	Text::String _text;
	QPoint _point;

	const style::Tooltip *_st = nullptr;

	base::Timer _hideByLeaveTimer;
	bool _isEventFilter = false;
	bool _useTransparency = true;

};

class ImportantTooltip : public RpWidget {
public:
	ImportantTooltip(
		QWidget *parent,
		object_ptr<RpWidget> content,
		const style::ImportantTooltip &st);

	void pointAt(
		QRect area,
		RectParts preferSide = RectPart::Top | RectPart::Left,
		Fn<QPoint(QSize)> countPosition = nullptr);

	void toggleAnimated(bool visible);
	void toggleFast(bool visible);
	void hideAfter(crl::time timeout);
	void updateGeometry();

	void setHiddenCallback(Fn<void()> callback) {
		_hiddenCallback = std::move(callback);
	}

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void animationCallback();
	[[nodiscard]] QRect countInner() const;
	void countApproachSide(RectParts preferSide);
	void resizeToContent();
	void checkAnimationFinish();
	void refreshAnimationCache();
	[[nodiscard]] QPoint countPosition() const;

	base::Timer _hideTimer;
	const style::ImportantTooltip &_st;
	object_ptr<RpWidget> _content;
	QRect _area;
	RectParts _side = RectPart::Top | RectPart::Left;
	QPixmap _arrow;

	Ui::Animations::Simple _visibleAnimation;
	Fn<QPoint(QSize)> _countPosition;
	bool _visible = false;
	Fn<void()> _hiddenCallback;
	QPixmap _cache;

};

[[nodiscard]] int FindNiceTooltipWidth(
	int minWidth,
	int maxWidth,
	Fn<int(int width)> heightForWidth);

[[nodiscard]] object_ptr<FlatLabel> MakeNiceTooltipLabel(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&text,
	int maxWidth,
	const style::FlatLabel &st = st::defaultFlatLabel,
	const style::PopupMenu &stMenu = st::defaultPopupMenu);

} // namespace Ui
