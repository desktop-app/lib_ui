// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/timer.h"
#include "ui/emoji_config.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_custom_emoji.h"

#include <QtGui/QTextObjectInterface>

#include <rpl/variable.h>

class QMenu;
class QTextEdit;
class QTouchEvent;
class QContextMenuEvent;
class Painter;

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace style {
struct InputField;
} // namespace style

namespace Ui {

const auto kClearFormatSequence = QKeySequence("ctrl+shift+n");
const auto kStrikeOutSequence = QKeySequence("ctrl+shift+x");
const auto kBlockquoteSequence = QKeySequence("ctrl+shift+.");
const auto kMonospaceSequence = QKeySequence("ctrl+shift+m");
const auto kEditLinkSequence = QKeySequence("ctrl+k");
const auto kSpoilerSequence = QKeySequence("ctrl+shift+p");

class PopupMenu;
class InputField;

void InsertEmojiAtCursor(QTextCursor cursor, EmojiPtr emoji);
void InsertCustomEmojiAtCursor(
	not_null<InputField*> field,
	QTextCursor cursor,
	const QString &text,
	const QString &link);

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

class CustomEmojiObject : public QObject, public QTextObjectInterface {
public:
	using Factory = Fn<std::unique_ptr<Text::CustomEmoji>(QStringView)>;

	CustomEmojiObject(Factory factory, Fn<bool()> paused);
	~CustomEmojiObject();

	void *qt_metacast(const char *iid) override;

	QSizeF intrinsicSize(
		QTextDocument *doc,
		int posInDocument,
		const QTextFormat &format) override;
	void drawObject(
		QPainter *painter,
		const QRectF &rect,
		QTextDocument *doc,
		int posInDocument,
		const QTextFormat &format) override;

	void setNow(crl::time now);
	void clear();

private:
	Factory _factory;
	Fn<bool()> _paused;
	base::flat_map<uint64, std::unique_ptr<Text::CustomEmoji>> _emoji;
	crl::time _now = 0;
	int _skip = 0;

};

class InputField : public RpWidget {
public:
	enum class Mode {
		SingleLine,
		NoNewlines,
		MultiLine,
	};
	using TagList = TextWithTags::Tags;
	using CustomEmojiFactory = Text::CustomEmojiFactory;

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
	static const QString kTagSpoiler;
	static const QString kTagBlockquote;
	static const QString kCustomEmojiTagStart;
	static const int kCustomEmojiFormat;

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

	[[nodiscard]] const style::InputField &st() const {
		return _st;
	}

	void showError();
	void showErrorNoFocus();
	void hideError();

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
	void setTagMimeProcessor(Fn<QString(QStringView)> processor);
	void setCustomEmojiFactory(
		CustomEmojiFactory factory,
		Fn<bool()> paused = nullptr);

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
		std::shared_ptr<QContextMenuEvent> event;
	};

	void setAdditionalMargin(int margin);
	void setAdditionalMargins(QMargins margins);

	void setInstantReplaces(const InstantReplaces &replaces);
	void setInstantReplacesEnabled(rpl::producer<bool> enabled);
	void setMarkdownReplacesEnabled(rpl::producer<bool> enabled);
	void setExtendedContextMenu(rpl::producer<ExtendedContextMenu> value);
	void commitInstantReplacement(
		int from,
		int till,
		const QString &with,
		const QString &customEmojiData);
	void commitMarkdownLinkEdit(
		EditLinkSelection selection,
		const QString &text,
		const QString &link);
	[[nodiscard]] static bool IsValidMarkdownLink(QStringView link);
	[[nodiscard]] static bool IsCustomEmojiLink(QStringView link);
	[[nodiscard]] static QString CustomEmojiLink(QStringView entityData);
	[[nodiscard]] static QString CustomEmojiEntityData(QStringView link);

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

	[[nodiscard]] bool menuShown() const;
	[[nodiscard]] rpl::producer<bool> menuShownValue() const;

	[[nodiscard]] rpl::producer<> heightChanges() const;
	[[nodiscard]] rpl::producer<bool> focusedChanges() const;
	[[nodiscard]] rpl::producer<> tabbed() const;
	[[nodiscard]] rpl::producer<> cancelled() const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] rpl::producer<Qt::KeyboardModifiers> submits() const;

	~InputField();

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
	void paintEventInner(QPaintEvent *e);

	void mousePressEventInner(QMouseEvent *e);
	void mouseReleaseEventInner(QMouseEvent *e);
	void mouseMoveEventInner(QMouseEvent *e);

	QMimeData *createMimeDataFromSelectionInner() const;
	bool canInsertFromMimeDataInner(const QMimeData *source) const;
	void insertFromMimeDataInner(const QMimeData *source);
	TextWithTags getTextWithTagsSelected() const;

	void documentContentsChanged(
		int position,
		int charsRemoved,
		int charsAdded);
	void focusInner();

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
		const QString &customEmojiData,
		std::optional<QString> checkOriginal,
		bool checkIfInMonospace);
#if 0
	bool commitMarkdownReplacement(
		int from,
		int till,
		const QString &tag,
		const QString &edge = QString());
#endif
	void addMarkdownTag(int from, int till, const QString &tag);
	void removeMarkdownTag(int from, int till, const QString &tag);
	void finishMarkdownTagChange(
		int from,
		int till,
		const TextWithTags &textWithTags);
	void toggleSelectionMarkdown(const QString &tag);
	void clearSelectionMarkdown();

	bool revertFormatReplace();

	void paintSurrounding(
		QPainter &p,
		QRect clip,
		float64 errorDegree,
		float64 focusedDegree);
	void paintRoundSurrounding(
		QPainter &p,
		QRect clip,
		float64 errorDegree,
		float64 focusedDegree);
	void paintFlatSurrounding(
		QPainter &p,
		QRect clip,
		float64 errorDegree,
		float64 focusedDegree);
	void customEmojiRepaint();
	void highlightMarkdown();

	void touchUpdate(QPoint globalPosition);
	void touchFinish();

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

	QMargins _additionalMargins;

	bool _forcePlaceholderHidden = false;
	bool _reverseMarkdownReplacement = false;
	bool _customEmojiRepaintScheduled = false;

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

	Fn<QString(QStringView)> _tagMimeProcessor;
	std::unique_ptr<CustomEmojiObject> _customEmojiObject;

	SubmitSettings _submitSettings = SubmitSettings::Enter;
	bool _markdownEnabled = false;
	bool _undoAvailable = false;
	bool _redoAvailable = false;
	bool _inDrop = false;
	bool _inHeightCheck = false;

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

	base::Timer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	bool _mousePressedInTouch = false;
	QPoint _touchStart;

	bool _correcting = false;
	MimeDataHook _mimeDataHook;
	rpl::event_stream<bool> _menuShownChanges;
	base::unique_qptr<PopupMenu> _contextMenu;

	QTextCharFormat _defaultCharFormat;

	rpl::variable<int> _scrollTop;

	InstantReplaces _mutableInstantReplaces;
	bool _instantReplacesEnabled = true;

	rpl::event_stream<DocumentChangeInfo> _documentContentsChanges;
	rpl::event_stream<MarkdownTag> _markdownTagApplies;

	rpl::event_stream<bool> _focusedChanges;
	rpl::event_stream<> _heightChanges;
	rpl::event_stream<> _tabbed;
	rpl::event_stream<> _cancelled;
	rpl::event_stream<> _changes;
	rpl::event_stream<Qt::KeyboardModifiers> _submits;

};

} // namespace Ui
