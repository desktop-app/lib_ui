// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/emoji_config.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/text/text_entity.h"
#include "styles/style_widgets.h"

#include <QContextMenuEvent>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTextEdit>
#include <QtCore/QTimer>

class QTouchEvent;
class Painter;

namespace Ui {

const auto kClearFormatSequence = QKeySequence("ctrl+shift+n");
const auto kStrikeOutSequence = QKeySequence("ctrl+shift+x");
const auto kMonospaceSequence = QKeySequence("ctrl+shift+m");
const auto kEditLinkSequence = QKeySequence("ctrl+k");

class PopupMenu;

void InsertEmojiAtCursor(QTextCursor cursor, EmojiPtr emoji);

struct InstantReplaces {
	struct Node {
		QString text;
		std::map<QChar, Node> tail;
	};

	void add(const QString &what, const QString &with);

	static const InstantReplaces &Default();
	static const InstantReplaces &TextOnly();

	int maxLength = 0;
	Node reverseMap;

};

enum class InputSubmitSettings {
	Enter,
	CtrlEnter,
	Both,
	None,
};

class FlatInput : public RpWidgetWrap<QLineEdit> {
	// The Q_OBJECT meta info is used for qobject_cast!
	Q_OBJECT

	using Parent = RpWidgetWrap<QLineEdit>;
public:
	FlatInput(
		QWidget *parent,
		const style::FlatInput &st,
		rpl::producer<QString> placeholder = nullptr,
		const QString &val = QString());

	void updatePlaceholder();
	void setPlaceholder(rpl::producer<QString> placeholder);
	QRect placeholderRect() const;

	void finishAnimations();

	void setTextMrg(const QMargins &textMrg);
	QRect getTextRect() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	const QString &getLastText() const {
		return _oldtext;
	}

public Q_SLOTS:
	void onTextChange(const QString &text);
	void onTextEdited();

	void onTouchTimer();

Q_SIGNALS:
	void changed();
	void cancelled();
	void submitted(Qt::KeyboardModifiers);
	void focused();
	void blurred();

protected:
	bool eventHook(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;

	virtual void correctValue(const QString &was, QString &now);

	style::font phFont() {
		return _st.font;
	}

	void phPrepare(QPainter &p, float64 placeholderFocused);

private:
	void updatePalette();
	void refreshPlaceholder(const QString &text);

	QString _oldtext;
	rpl::variable<QString> _placeholderFull;
	QString _placeholder;

	bool _customUpDown = false;

	bool _focused = false;
	bool _placeholderVisible = true;
	Animations::Simple _placeholderFocusedAnimation;
	Animations::Simple _placeholderVisibleAnimation;
	bool _lastPreEditTextNotEmpty = false;

	const style::FlatInput &_st;
	QMargins _textMrg;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;
};

class InputField : public RpWidget {
	Q_OBJECT

public:
	enum class Mode {
		SingleLine,
		NoNewlines,
		MultiLine,
	};
	using TagList = TextWithTags::Tags;

	struct MarkdownTag {
		// With each emoji being QChar::ObjectReplacementCharacter.
		int internalStart = 0;
		int internalLength = 0;

		// Adjusted by emoji to match _lastTextWithTags.
		int adjustedStart = 0;
		int adjustedLength = 0;

		bool closed = false;
		QString tag;
	};
	static const QString kTagBold;
	static const QString kTagItalic;
	static const QString kTagUnderline;
	static const QString kTagStrikeOut;
	static const QString kTagCode;
	static const QString kTagPre;

	InputField(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &value = QString());
	InputField(
		QWidget *parent,
		const style::InputField &st,
		Mode mode,
		rpl::producer<QString> placeholder,
		const QString &value);
	InputField(
		QWidget *parent,
		const style::InputField &st,
		Mode mode = Mode::SingleLine,
		rpl::producer<QString> placeholder = nullptr,
		const TextWithTags &value = TextWithTags());

	void showError();
	void showErrorNoFocus();

	void setMaxLength(int maxLength);
	void setMinHeight(int minHeight);
	void setMaxHeight(int maxHeight);

	const TextWithTags &getTextWithTags() const {
		return _lastTextWithTags;
	}
	const std::vector<MarkdownTag> &getMarkdownTags() const {
		return _lastMarkdownTags;
	}
	TextWithTags getTextWithTagsPart(int start, int end = -1) const;
	TextWithTags getTextWithAppliedMarkdown() const;
	void insertTag(const QString &text, QString tagId = QString());
	bool empty() const {
		return _lastTextWithTags.text.isEmpty();
	}
	enum class HistoryAction {
		NewEntry,
		MergeEntry,
		Clear,
	};
	void setTextWithTags(
		const TextWithTags &textWithTags,
		HistoryAction historyAction = HistoryAction::NewEntry);

	// If you need to make some preparations of tags before putting them to QMimeData
	// (and then to clipboard or to drag-n-drop object), here is a strategy for that.
	class TagMimeProcessor {
	public:
		virtual QString tagFromMimeTag(const QString &mimeTag) = 0;
		virtual ~TagMimeProcessor() = default;
	};
	void setTagMimeProcessor(std::unique_ptr<TagMimeProcessor> &&processor);

	struct EditLinkSelection {
		int from = 0;
		int till = 0;
	};
	enum class EditLinkAction {
		Check,
		Edit,
	};
	void setEditLinkCallback(
		Fn<bool(
			EditLinkSelection selection,
			QString text,
			QString link,
			EditLinkAction action)> callback);

	struct ExtendedContextMenu {
		QMenu *menu = nullptr;
		QContextMenuEvent event;
	};

	void setAdditionalMargin(int margin);

	void setInstantReplaces(const InstantReplaces &replaces);
	void setInstantReplacesEnabled(rpl::producer<bool> enabled);
	void setMarkdownReplacesEnabled(rpl::producer<bool> enabled);
	void setExtendedContextMenu(rpl::producer<ExtendedContextMenu> value);
	void commitInstantReplacement(int from, int till, const QString &with);
	void commitMarkdownLinkEdit(
		EditLinkSelection selection,
		const QString &text,
		const QString &link);
	static bool IsValidMarkdownLink(const QString &link);

	const QString &getLastText() const {
		return _lastTextWithTags.text;
	}
	void setPlaceholder(
		rpl::producer<QString> placeholder,
		int afterSymbols = 0);
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void setDisplayFocused(bool focused);
	void finishAnimating();
	void setFocusFast() {
		setDisplayFocused(true);
		setFocus();
	}

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	bool hasText() const;
	void selectAll();

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	bool isMarkdownEnabled() const {
		return _markdownEnabled;
	}

	using SubmitSettings = InputSubmitSettings;
	void setSubmitSettings(SubmitSettings settings);
	static bool ShouldSubmit(
		SubmitSettings settings,
		Qt::KeyboardModifiers modifiers);
	void customUpDown(bool isCustom);
	void customTab(bool isCustom);
	int borderAnimationStart() const;

	not_null<QTextDocument*> document();
	not_null<const QTextDocument*> document() const;
	void setTextCursor(const QTextCursor &cursor);
	void setCursorPosition(int position);
	QTextCursor textCursor() const;
	void setText(const QString &text);
	void clear();
	bool hasFocus() const;
	void setFocus();
	void clearFocus();
	void ensureCursorVisible();
	not_null<QTextEdit*> rawTextEdit();
	not_null<const QTextEdit*> rawTextEdit() const;

	enum class MimeAction {
		Check,
		Insert,
	};
	using MimeDataHook = Fn<bool(
		not_null<const QMimeData*> data,
		MimeAction action)>;
	void setMimeDataHook(MimeDataHook hook) {
		_mimeDataHook = std::move(hook);
	}

	const rpl::variable<int> &scrollTop() const;
	int scrollTopMax() const;
	void scrollTo(int top);

	struct DocumentChangeInfo {
		int position = 0;
		int added = 0;
		int removed = 0;
	};
	auto documentContentsChanges() {
		return _documentContentsChanges.events();
	}
	auto markdownTagApplies() {
		return _markdownTagApplies.events();
	}

	~InputField();

private Q_SLOTS:
	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onCursorPositionChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void onFocusInner();

Q_SIGNALS:
	void changed();
	void submitted(Qt::KeyboardModifiers);
	void cancelled();
	void tabbed();
	void focused();
	void blurred();
	void resized();

protected:
	void startPlaceholderAnimation();
	void startBorderAnimation();

	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;
	friend class Inner;

	void handleContentsChanged();
	bool viewportEventInner(QEvent *e);
	void handleTouchEvent(QTouchEvent *e);

	void updatePalette();
	void refreshPlaceholder(const QString &text);
	int placeholderSkipWidth() const;

	bool heightAutoupdated();
	void checkContentHeight();
	void setErrorShown(bool error);

	void focusInEventInner(QFocusEvent *e);
	void focusOutEventInner(QFocusEvent *e);
	void setFocused(bool focused);
	void keyPressEventInner(QKeyEvent *e);
	void contextMenuEventInner(QContextMenuEvent *e, QMenu *m = nullptr);
	void dropEventInner(QDropEvent *e);
	void inputMethodEventInner(QInputMethodEvent *e);

	QMimeData *createMimeDataFromSelectionInner() const;
	bool canInsertFromMimeDataInner(const QMimeData *source) const;
	void insertFromMimeDataInner(const QMimeData *source);
	TextWithTags getTextWithTagsSelected() const;

	// "start" and "end" are in coordinates of text where emoji are replaced
	// by ObjectReplacementCharacter. If "end" = -1 means get text till the end.
	QString getTextPart(
		int start,
		int end,
		TagList &outTagsList,
		bool &outTagsChanged,
		std::vector<MarkdownTag> *outMarkdownTags = nullptr) const;

	// After any characters added we must postprocess them. This includes:
	// 1. Replacing font family to semibold for ~ characters, if we used Open Sans 13px.
	// 2. Replacing font family from semibold for all non-~ characters, if we used ...
	// 3. Replacing emoji code sequences by ObjectReplacementCharacters with emoji pics.
	// 4. Interrupting tags in which the text was inserted by any char except a letter.
	// 5. Applying tags from "_insertedTags" in case we pasted text with tags, not just text.
	// Rule 4 applies only if we inserted chars not in the middle of a tag (but at the end).
	void processFormatting(int changedPosition, int changedEnd);

	void chopByMaxLength(int insertPosition, int insertLength);

	bool processMarkdownReplaces(const QString &appended);
	//bool processMarkdownReplace(const QString &tag);
	void addMarkdownActions(not_null<QMenu*> menu, QContextMenuEvent *e);
	void addMarkdownMenuAction(
		not_null<QMenu*> menu,
		not_null<QAction*> action);
	bool handleMarkdownKey(QKeyEvent *e);

	// We don't want accidentally detach InstantReplaces map.
	// So we access it only by const reference from this method.
	const InstantReplaces &instantReplaces() const;
	void processInstantReplaces(const QString &appended);
	void applyInstantReplace(const QString &what, const QString &with);

	struct EditLinkData {
		int from = 0;
		int till = 0;
		QString link;
	};
	EditLinkData selectionEditLinkData(EditLinkSelection selection) const;
	EditLinkSelection editLinkSelection(QContextMenuEvent *e) const;
	void editMarkdownLink(EditLinkSelection selection);

	void commitInstantReplacement(
		int from,
		int till,
		const QString &with,
		std::optional<QString> checkOriginal,
		bool checkIfInMonospace);
	bool commitMarkdownReplacement(
		int from,
		int till,
		const QString &tag,
		const QString &edge = QString());
	void toggleSelectionMarkdown(const QString &tag);
	void clearSelectionMarkdown();

	bool revertFormatReplace();

	void highlightMarkdown();

	const style::InputField &_st;

	Mode _mode = Mode::SingleLine;
	int _maxLength = -1;
	int _minHeight = -1;
	int _maxHeight = -1;

	const std::unique_ptr<Inner> _inner;

	Fn<bool(
		EditLinkSelection selection,
		QString text,
		QString link,
		EditLinkAction action)> _editLinkCallback;
	TextWithTags _lastTextWithTags;
	std::vector<MarkdownTag> _lastMarkdownTags;
	QString _lastPreEditText;
	std::optional<QString> _inputMethodCommit;

	bool _forcePlaceholderHidden = false;
	bool _reverseMarkdownReplacement = false;

	// Tags list which we should apply while setText() call or insert from mime data.
	TagList _insertedTags;
	bool _insertedTagsAreFromMime;

	// Override insert position and charsAdded from complex text editing
	// (like drag-n-drop in the same text edit field).
	int _realInsertPosition = -1;
	int _realCharsAdded = 0;

	// Calculate the amount of emoji extra chars
	// before _documentContentsChanges fire.
	int _emojiSurrogateAmount = 0;

	std::unique_ptr<TagMimeProcessor> _tagMimeProcessor;

	SubmitSettings _submitSettings = SubmitSettings::Enter;
	bool _markdownEnabled = false;
	bool _undoAvailable = false;
	bool _redoAvailable = false;
	bool _inDrop = false;
	bool _inHeightCheck = false;
	int _additionalMargin = 0;

	bool _customUpDown = false;
	bool _customTab = false;

	rpl::variable<QString> _placeholderFull;
	QString _placeholder;
	int _placeholderAfterSymbols = 0;
	Animations::Simple _a_placeholderShifted;
	bool _placeholderShifted = false;
	QPainterPath _placeholderPath;

	Animations::Simple _a_borderShown;
	int _borderAnimationStart = 0;
	Animations::Simple _a_borderOpacity;
	bool _borderVisible = false;

	Animations::Simple _a_focused;
	Animations::Simple _a_error;

	bool _focused = false;
	bool _error = false;

	QTimer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	QPoint _touchStart;

	bool _correcting = false;
	MimeDataHook _mimeDataHook;
	base::unique_qptr<PopupMenu> _contextMenu;

	QTextCharFormat _defaultCharFormat;

	rpl::variable<int> _scrollTop;

	InstantReplaces _mutableInstantReplaces;
	bool _instantReplacesEnabled = true;

	rpl::event_stream<DocumentChangeInfo> _documentContentsChanges;
	rpl::event_stream<MarkdownTag> _markdownTagApplies;

};

class MaskedInputField : public RpWidgetWrap<QLineEdit> {
	// The Q_OBJECT meta info is used for qobject_cast!
	Q_OBJECT

	using Parent = RpWidgetWrap<QLineEdit>;
public:
	MaskedInputField(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder = nullptr,
		const QString &val = QString());

	void showError();

	QRect getTextRect() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	int borderAnimationStart() const;

	const QString &getLastText() const {
		return _oldtext;
	}
	void setPlaceholder(rpl::producer<QString> placeholder);
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void setDisplayFocused(bool focused);
	void finishAnimating();
	void setFocusFast() {
		setDisplayFocused(true);
		setFocus();
	}

	void setText(const QString &text) {
		QLineEdit::setText(text);
		startPlaceholderAnimation();
	}
	void clear() {
		QLineEdit::clear();
		startPlaceholderAnimation();
	}

public Q_SLOTS:
	void onTextChange(const QString &text);
	void onCursorPositionChanged(int oldPosition, int position);

	void onTextEdited();

	void onTouchTimer();

Q_SIGNALS:
	void changed();
	void cancelled();
	void submitted(Qt::KeyboardModifiers);
	void focused();
	void blurred();

protected:
	QString getDisplayedText() const {
		auto result = getLastText();
		if (!_lastPreEditText.isEmpty()) {
			result = result.mid(0, _oldcursor) + _lastPreEditText + result.mid(_oldcursor);
		}
		return result;
	}
	void startBorderAnimation();
	void startPlaceholderAnimation();

	bool eventHook(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;

	virtual void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	}
	void setCorrectedText(QString &now, int &nowCursor, const QString &newText, int newPos);

	virtual void paintAdditionalPlaceholder(Painter &p) {
	}

	style::font phFont() {
		return _st.font;
	}

	void placeholderAdditionalPrepare(Painter &p);
	QRect placeholderRect() const;

	void setTextMargins(const QMargins &mrg);
	const style::InputField &_st;

private:
	void updatePalette();
	void refreshPlaceholder(const QString &text);
	void setErrorShown(bool error);

	void setFocused(bool focused);

	int _maxLength = -1;
	bool _forcePlaceholderHidden = false;

	QString _oldtext;
	int _oldcursor = 0;
	QString _lastPreEditText;

	bool _undoAvailable = false;
	bool _redoAvailable = false;

	bool _customUpDown = false;

	rpl::variable<QString> _placeholderFull;
	QString _placeholder;
	Animations::Simple _a_placeholderShifted;
	bool _placeholderShifted = false;
	QPainterPath _placeholderPath;

	Animations::Simple _a_borderShown;
	int _borderAnimationStart = 0;
	Animations::Simple _a_borderOpacity;
	bool _borderVisible = false;

	Animations::Simple _a_focused;
	Animations::Simple _a_error;

	bool _focused = false;
	bool _error = false;

	style::margins _textMargins;

	QTimer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	QPoint _touchStart;
};

class PasswordInput : public MaskedInputField {
public:
	PasswordInput(QWidget *parent, const style::InputField &st, rpl::producer<QString> placeholder = nullptr, const QString &val = QString());

};

class NumberInput : public MaskedInputField {
public:
	NumberInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &value,
		int limit);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	int _limit = 0;

};

class HexInput : public MaskedInputField {
public:
	HexInput(QWidget *parent, const style::InputField &st, rpl::producer<QString> placeholder, const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

} // namespace Ui
