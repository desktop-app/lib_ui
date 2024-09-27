// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "ui/rp_widget.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/text.h"
#include "ui/click_handler.h"
#include "ui/widgets/box_content_divider.h"
#include "styles/style_widgets.h"

class QTouchEvent;

namespace Ui {

class PopupMenu;
class BoxContentDivider;

class CrossFadeAnimation {
public:
	struct Data {
		QImage full;
		std::vector<int> lineWidths;
		QPoint position;
		style::align align;
		style::font font;
		style::margins margin;
		int lineHeight = 0;
		int lineAddTop = 0;
	};

	CrossFadeAnimation(style::color bg, Data &&from, Data &&to);

	struct Part {
		QPixmap snapshot;
		QPoint position;
	};
	void addLine(Part was, Part now);

	void paintFrame(QPainter &p, float64 dt);
	void paintFrame(
		QPainter &p,
		float64 positionReady,
		float64 alphaWas,
		float64 alphaNow);

private:
	struct Line {
		Line(Part was, Part now) : was(std::move(was)), now(std::move(now)) {
		}
		Part was;
		Part now;
	};
	void paintLine(
		QPainter &p,
		const Line &line,
		float64 positionReady,
		float64 alphaWas,
		float64 alphaNow);

	style::color _bg;
	QList<Line> _lines;

};

class LabelSimple : public RpWidget {
public:
	LabelSimple(
		QWidget *parent,
		const style::LabelSimple &st = st::defaultLabelSimple,
		const QString &value = QString());

	// This method also resizes the label.
	void setText(const QString &newText, bool *outTextChanged = nullptr);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _fullText;
	int _fullTextWidth;

	QString _text;
	int _textWidth;

	const style::LabelSimple &_st;

};

class FlatLabel : public RpWidget, public ClickHandlerHost {
public:
	FlatLabel(
		QWidget *parent,
		const style::FlatLabel &st = st::defaultFlatLabel,
		const style::PopupMenu &stMenu = st::defaultPopupMenu);

	FlatLabel(
		QWidget *parent,
		const QString &text,
		const style::FlatLabel &st = st::defaultFlatLabel,
		const style::PopupMenu &stMenu = st::defaultPopupMenu);

	FlatLabel(
		QWidget *parent,
		rpl::producer<QString> &&text,
		const style::FlatLabel &st = st::defaultFlatLabel,
		const style::PopupMenu &stMenu = st::defaultPopupMenu);
	FlatLabel(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&text,
		const style::FlatLabel &st = st::defaultFlatLabel,
		const style::PopupMenu &stMenu = st::defaultPopupMenu,
		const Fn<std::any(Fn<void()>)> &makeContext = nullptr);

	[[nodiscard]] const style::FlatLabel &st() const {
		return _st;
	}

	void setOpacity(float64 o);
	void setTextColorOverride(std::optional<QColor> color);

	void setText(const QString &text);
	void setMarkedText(
		const TextWithEntities &textWithEntities,
		const std::any &context = {});
	void setSelectable(bool selectable);
	void setDoubleClickSelectsParagraph(bool doubleClickSelectsParagraph);
	void setContextCopyText(const QString &copyText);
	void setBreakEverywhere(bool breakEverywhere);
	void setTryMakeSimilarLines(bool tryMakeSimilarLines);
	enum class WhichAnimationsPaused {
		None,
		CustomEmoji,
		Spoiler,
		All,
	};
	void setAnimationsPausedCallback(Fn<WhichAnimationsPaused()> callback) {
		_animationsPausedCallback = std::move(callback);
	}

	[[nodiscard]] int textMaxWidth() const;
	int naturalWidth() const override;
	QMargins getMargins() const override;

	void setLink(uint16 index, const ClickHandlerPtr &lnk);
	void setLinksTrusted();

	using ClickHandlerFilter = Fn<bool(const ClickHandlerPtr&, Qt::MouseButton)>;
	void setClickHandlerFilter(ClickHandlerFilter &&filter);
	void overrideLinkClickHandler(Fn<void()> handler);
	void overrideLinkClickHandler(Fn<void(QString url)> handler);

	struct ContextMenuRequest {
		not_null<PopupMenu*> menu;
		ClickHandlerPtr link;
		TextSelection selection;
		bool uponSelection = false;
		bool fullSelection = false;
	};
	void setContextMenuHook(Fn<void(ContextMenuRequest)> hook);
	void fillContextMenu(ContextMenuRequest request);

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &action, bool pressed) override;

	[[nodiscard]] CrossFadeAnimation::Data crossFadeData(
		style::color bg,
		QPoint basePosition = QPoint());

	static std::unique_ptr<CrossFadeAnimation> CrossFade(
		not_null<FlatLabel*> from,
		not_null<FlatLabel*> to,
		style::color bg,
		QPoint fromPosition = QPoint(),
		QPoint toPosition = QPoint());

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	bool eventHook(QEvent *e) override; // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);

	int resizeGetHeight(int newWidth) override;

	void copySelectedText();
	void copyContextText();

	void touchSelect();

	void executeDrag();

private:
	void init();
	void textUpdated();

	Text::StateResult dragActionUpdate();
	Text::StateResult dragActionStart(const QPoint &p, Qt::MouseButton button);
	Text::StateResult dragActionFinish(const QPoint &p, Qt::MouseButton button);
	void updateHover(const Text::StateResult &state);
	Text::StateResult getTextState(const QPoint &m) const;
	void refreshCursor(bool uponSymbol);

	int countTextWidth() const;
	int countTextHeight(int textWidth);
	void refreshSize();

	enum class ContextMenuReason {
		FromEvent,
		FromTouch,
	};
	void showContextMenu(QContextMenuEvent *e, ContextMenuReason reason);

	Text::String _text;
	const style::FlatLabel &_st;
	const style::PopupMenu &_stMenu;
	std::optional<QColor> _textColorOverride;
	float64 _opacity = 1.;

	int _allowedWidth = 0;
	int _textWidth = 0;
	int _fullTextHeight = 0;
	bool _breakEverywhere = false;
	bool _tryMakeSimilarLines = false;

	style::cursor _cursor = style::cur_default;
	bool _selectable = false;
	TextSelection _selection, _savedSelection;
	TextSelectType _selectionType = TextSelectType::Letters;
	bool _doubleClickSelectsParagraph = false;

	enum DragAction {
		NoDrag = 0x00,
		PrepareDrag = 0x01,
		Dragging = 0x02,
		Selecting = 0x04,
	};
	DragAction _dragAction = NoDrag;
	QPoint _dragStartPosition;
	uint16 _dragSymbol = 0;
	bool _dragWasInactive = false;

	QPoint _lastMousePos;

	QPoint _trippleClickPoint;
	base::Timer _trippleClickTimer;

	base::unique_qptr<PopupMenu> _contextMenu;
	Fn<void(ContextMenuRequest)> _contextMenuHook;
	QString _contextCopyText;

	ClickHandlerFilter _clickHandlerFilter;
	Fn<WhichAnimationsPaused()> _animationsPausedCallback;

	// text selection and context menu by touch support (at least Windows Surface tablets)
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	base::Timer _touchSelectTimer;

};

class DividerLabel : public PaddingWrap<> {
public:
	DividerLabel(
		QWidget *parent,
		object_ptr<RpWidget> &&child,
		const style::margins &padding,
		RectParts parts = RectPart::Top | RectPart::Bottom);

	int naturalWidth() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<BoxContentDivider> _background;

};

} // namespace Ui
