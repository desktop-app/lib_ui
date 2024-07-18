// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/flat_set.h"
#include "base/timer.h"
#include "ui/emoji_config.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_custom_emoji.h"

#include <rpl/variable.h>

#include <QtGui/QTextCursor>

class QMenu;
class QShortcut;
class QTextEdit;
class QTouchEvent;
class QContextMenuEvent;
class Painter;

namespace Ui::Text {
struct QuotePaintCache;
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

class CustomFieldObject;

struct MarkdownEnabled {
	base::flat_set<QString> tagsSubset;

	friend inline bool operator==(
		const MarkdownEnabled &,
		const MarkdownEnabled &) = default;
};
struct MarkdownDisabled {
	friend inline bool operator==(
		const MarkdownDisabled &,
		const MarkdownDisabled &) = default;
};
struct MarkdownEnabledState {
	std::variant<MarkdownDisabled, MarkdownEnabled> data;

	[[nodiscard]] bool disabled() const;
	[[nodiscard]] bool enabledForTag(QStringView tag) const;

	friend inline bool operator==(
		const MarkdownEnabledState &,
		const MarkdownEnabledState &) = default;
};
struct InputFieldTextRange {
	int from = 0;
	int till = 0;

	friend inline bool operator==(
		InputFieldTextRange,
		InputFieldTextRange) = default;

	[[nodiscard]] bool empty() const {
		return (till <= from);
	}
};
struct InputFieldSpoilerRect {
	QRect geometry;
	bool blockquote = false;
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
	static const QString kTagBlockquoteCollapsed;
	static const QString kCustomEmojiTagStart;
	static const int kCollapsedQuoteFormat; // QTextFormat::ObjectTypes
	static const int kCustomEmojiFormat; // QTextFormat::ObjectTypes
	static const int kCustomEmojiId; // QTextFormat::Property
	static const int kCustomEmojiLink; // QTextFormat::Property
	static const int kQuoteId; // QTextFormat::Property

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

	[[nodiscard]] const TextWithTags &getTextWithTags() const {
		return _lastTextWithTags;
	}
	[[nodiscard]] const std::vector<MarkdownTag> &getMarkdownTags() const {
		return _lastMarkdownTags;
	}
	[[nodiscard]] TextWithTags getTextWithTagsPart(
		int start,
		int end = -1) const;
	[[nodiscard]] TextWithTags getTextWithAppliedMarkdown() const;
	void insertTag(const QString &text, QString tagId = QString());
	[[nodiscard]] bool empty() const {
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
	void setCustomTextContext(
		Fn<std::any(Fn<void()> repaint)> context,
		Fn<bool()> pausedEmoji = nullptr,
		Fn<bool()> pausedSpoiler = nullptr,
		CustomEmojiFactory factory = nullptr);

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
	void setEditLanguageCallback(
		Fn<void(QString now, Fn<void(QString)> save)> callback);

	struct ExtendedContextMenu {
		QMenu *menu = nullptr;
		std::shared_ptr<QContextMenuEvent> event;
	};

	void setDocumentMargin(float64 margin);
	void setAdditionalMargin(int margin);
	void setAdditionalMargins(QMargins margins);

	void setInstantReplaces(const InstantReplaces &replaces);
	void setInstantReplacesEnabled(rpl::producer<bool> enabled);
	void setMarkdownReplacesEnabled(bool enabled);
	void setMarkdownReplacesEnabled(rpl::producer<MarkdownEnabledState> enabled);
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

	[[nodiscard]] const QString &getLastText() const {
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

	[[nodiscard]] MarkdownEnabledState markdownEnabledState() const {
		return _markdownEnabledState;
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

	void setPreCache(Fn<not_null<Ui::Text::QuotePaintCache*>()> make);
	void setBlockquoteCache(Fn<not_null<Ui::Text::QuotePaintCache*>()> make);

	[[nodiscard]] bool menuShown() const;
	[[nodiscard]] rpl::producer<bool> menuShownValue() const;

	[[nodiscard]] rpl::producer<> heightChanges() const;
	[[nodiscard]] rpl::producer<bool> focusedChanges() const;
	[[nodiscard]] rpl::producer<> tabbed() const;
	[[nodiscard]] rpl::producer<> cancelled() const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] rpl::producer<Qt::KeyboardModifiers> submits() const;
	void forceProcessContentsChanges();

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
	friend class CustomFieldObject;
	friend class FieldSpoilerOverlay;
	using TextRange = InputFieldTextRange;
	using SpoilerRect = InputFieldSpoilerRect;
	enum class MarkdownActionType {
		ToggleTag,
		EditLink,
	};
	struct MarkdownAction {
		QKeySequence sequence;
		QString tag;
		MarkdownActionType type = MarkdownActionType::ToggleTag;
	};

	void handleContentsChanged();
	void updateRootFrameFormat();
	bool viewportEventInner(QEvent *e);
	void handleTouchEvent(QTouchEvent *e);

	void updatePalette();
	void refreshPlaceholder(const QString &text);
	int placeholderSkipWidth() const;

	[[nodiscard]] static std::vector<MarkdownAction> MarkdownActions();
	void setupMarkdownShortcuts();
	bool executeMarkdownAction(MarkdownAction action);

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
	void paintQuotes(QPaintEvent *e);

	void mousePressEventInner(QMouseEvent *e);
	void mouseReleaseEventInner(QMouseEvent *e);
	void mouseMoveEventInner(QMouseEvent *e);
	void leaveEventInner(QEvent *e);

	[[nodiscard]] int lookupActionQuoteId(QPoint point) const;
	void updateCursorShape();

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
	[[nodiscard]] QString getTextPart(
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
	TextRange insertWithTags(TextRange range, TextWithTags text);
	TextRange addMarkdownTag(TextRange range, const QString &tag);
	void removeMarkdownTag(TextRange range, const QString &tag);
	void finishMarkdownTagChange(
		TextRange range,
		const TextWithTags &textWithTags);
	void toggleSelectionMarkdown(const QString &tag);
	void clearSelectionMarkdown();

	bool revertFormatReplace();
	bool jumpOutOfBlockByBackspace();

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
	bool exitQuoteWithNewBlock(int key);

	void blockActionClicked(int quoteId);
	void editPreLanguage(int quoteId, QStringView tag);
	void toggleBlockquoteCollapsed(
		int quoteId,
		QStringView tag,
		TextRange range);
	void trippleEnterExitBlock(QTextCursor &cursor);

	void touchUpdate(QPoint globalPosition);
	void touchFinish();

	const style::InputField &_st;
	Fn<not_null<Ui::Text::QuotePaintCache*>()> _preCache;
	Fn<not_null<Ui::Text::QuotePaintCache*>()> _blockquoteCache;

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
	Fn<void(QString now, Fn<void(QString)> save)> _editLanguageCallback;
	TextWithTags _lastTextWithTags;
	std::vector<MarkdownTag> _lastMarkdownTags;
	QString _lastPreEditText;
	std::optional<QString> _inputMethodCommit;
	mutable std::vector<TextRange> _spoilerRangesText;
	mutable std::vector<TextRange> _spoilerRangesEmoji;
	mutable std::vector<SpoilerRect> _spoilerRects;
	mutable QColor _blockquoteBg;
	std::unique_ptr<RpWidget> _spoilerOverlay;

	QMargins _additionalMargins;
	QMargins _customFontMargins;
	int _placeholderCustomFontSkip = 0;
	int _requestedDocumentTopMargin = 0;

	bool _forcePlaceholderHidden = false;
	bool _reverseMarkdownReplacement = false;
	bool _customEmojiRepaintScheduled = false;
	bool _settingDocumentMargin = false;

	// Tags list which we should apply while setText() call or insert from mime data.
	TagList _insertedTags;
	bool _insertedTagsAreFromMime = false;
	bool _insertedTagsReplace = false;

	// Override insert position and charsAdded from complex text editing
	// (like drag-n-drop in the same text edit field).
	int _realInsertPosition = -1;
	int _realCharsAdded = 0;

	// Calculate the amount of emoji extra chars
	// before _documentContentsChanges fire.
	int _emojiSurrogateAmount = 0;

	Fn<QString(QStringView)> _tagMimeProcessor;
	std::unique_ptr<CustomFieldObject> _customObject;
	std::optional<QTextCursor> _formattingCursorUpdate;

	SubmitSettings _submitSettings = SubmitSettings::Enter;
	MarkdownEnabledState _markdownEnabledState;
	bool _undoAvailable = false;
	bool _redoAvailable = false;
	bool _insertedTagsDelayClear = false;
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

	int _selectedActionQuoteId = 0;
	int _pressedActionQuoteId = -1;
	rpl::variable<int> _scrollTop;

	InstantReplaces _mutableInstantReplaces;
	bool _instantReplacesEnabled = true;

	rpl::event_stream<DocumentChangeInfo> _documentContentsChanges;
	rpl::event_stream<MarkdownTag> _markdownTagApplies;

	std::vector<std::unique_ptr<QShortcut>> _markdownShortcuts;

	rpl::event_stream<bool> _focusedChanges;
	rpl::event_stream<> _heightChanges;
	rpl::event_stream<> _tabbed;
	rpl::event_stream<> _cancelled;
	rpl::event_stream<> _changes;
	rpl::event_stream<Qt::KeyboardModifiers> _submits;

};

void PrepareFormattingOptimization(not_null<QTextDocument*> document);

[[nodiscard]] int ComputeRealUnicodeCharactersCount(const QString &text);
[[nodiscard]] int ComputeFieldCharacterCount(not_null<InputField*> field);

void AddLengthLimitLabel(not_null<InputField*> field, int limit);

} // namespace Ui
