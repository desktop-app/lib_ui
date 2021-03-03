// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "styles/style_widgets.h"
#include "ui/rp_widget.h"
#include "ui/round_rect.h"
#include "ui/effects/animations.h"
#include "ui/effects/panel_animation.h"
#include "base/object_ptr.h"

#include <QtCore/QTimer>

namespace Ui {

class ScrollArea;

class InnerDropdown : public RpWidget {
	Q_OBJECT

public:
	InnerDropdown(QWidget *parent, const style::InnerDropdown &st = st::defaultInnerDropdown);

	template <typename Widget>
	QPointer<Widget> setOwnedWidget(object_ptr<Widget> widget) {
		auto result = doSetOwnedWidget(std::move(widget));
		return QPointer<Widget>(static_cast<Widget*>(result.data()));
	}

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || _a_show.animating() || _a_opacity.animating()) return false;

		return rect().marginsRemoved(_st.padding).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void setAutoHiding(bool autoHiding) {
		_autoHiding = autoHiding;
	}
	void setMaxHeight(int newMaxHeight);
	void resizeToContent();

	void otherEnter();
	void otherLeave();

	void setShowStartCallback(Fn<void()> callback) {
		_showStartCallback = std::move(callback);
	}
	void setHideStartCallback(Fn<void()> callback) {
		_hideStartCallback = std::move(callback);
	}
	void setHiddenCallback(Fn<void()> callback) {
		_hiddenCallback = std::move(callback);
	}

	bool isHiding() const {
		return _hiding && _a_opacity.animating();
	}

	enum class HideOption {
		Default,
		IgnoreShow,
	};
	void showAnimated();
	void setOrigin(PanelAnimation::Origin origin);
	void showAnimated(PanelAnimation::Origin origin);
	void hideAnimated(HideOption option = HideOption::Default);
	void finishAnimating();
	void showFast();
	void hideFast();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private Q_SLOTS:
	void onHideAnimated() {
		hideAnimated();
	}
	void onScroll();

private:
	QPointer<RpWidget> doSetOwnedWidget(object_ptr<RpWidget> widget);
	QImage grabForPanelAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();

	class Container;
	void showAnimationCallback();
	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	void updateHeight();

	const style::InnerDropdown &_st;

	RoundRect _roundRect;
	PanelAnimation::Origin _origin = PanelAnimation::Origin::TopLeft;
	std::unique_ptr<PanelAnimation> _showAnimation;
	Animations::Simple _a_show;

	bool _autoHiding = true;
	bool _hiding = false;
	QPixmap _cache;
	Animations::Simple _a_opacity;

	QTimer _hideTimer;
	bool _ignoreShowEvents = false;
	Fn<void()> _showStartCallback;
	Fn<void()> _hideStartCallback;
	Fn<void()> _hiddenCallback;

	object_ptr<ScrollArea> _scroll;

	int _maxHeight = 0;

};

class InnerDropdown::Container : public TWidget {
public:
	Container(QWidget *parent, object_ptr<TWidget> child, const style::InnerDropdown &st);

	void resizeToContent();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	object_ptr<TWidget> _child;
	const style::InnerDropdown &_st;

};

} // namespace Ui
