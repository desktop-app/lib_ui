// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/widgets/fields/input_field.h"

#include <QtGui/QTextBlock>
#include <QtGui/QTextLayout>
#include <QtWidgets/QTextEdit>

namespace Ui {
namespace {

[[nodiscard]] QString Composition(not_null<QTextEdit*> edit) {
	return edit->textCursor().block().layout()->preeditAreaText();
}

[[nodiscard]] bool HasMimeData(not_null<QInputMethodEvent*> e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	for (const auto &attribute : e->attributes()) {
		if (attribute.type == QInputMethodEvent::MimeData) {
			return true;
		}
	}
#endif // Qt >= 6.10
	return false;
}

} // namespace

bool InputField::InputMethodCommit::Perform(
		not_null<QTextEdit*> edit,
		not_null<QInputMethodEvent*> e,
		Fn<void(not_null<QInputMethodEvent*>)> forward) {
	auto removal = edit->textCursor();
	if (removal.isNull()
		|| !(edit->textInteractionFlags() & Qt::TextEditable)
		|| HasMimeData(e)) {
		return false;
	}
	const auto commit = e->commitString();
	const auto attributes = e->attributes();
	const auto replacing = !commit.isEmpty() || e->replacementLength();
	const auto getting = !commit.isEmpty()
		|| (e->preeditString() != Composition(edit))
		|| (e->replacementLength() > 0);
	const auto removing = removal.hasSelection();
	if (!getting || (!removing && !replacing)) {
		return false;
	}
	const auto alive = QPointer<QTextEdit>(edit.get());
	auto open = removing;
	if (removing) {
		removal.beginEditBlock();
		removal.removeSelectedText();
	}
	if (replacing && !Composition(edit).isEmpty()) {
		if (base::take(open)) {
			removal.endEditBlock();
		}
		if (alive) {
			auto clear = QInputMethodEvent();
			forward(&clear);
		}
	}
	if (!alive) {
		return true;
	}
	auto insertion = edit->textCursor();
	if (!removing) {
		insertion.beginEditBlock();
	} else if (!open) {
		insertion.joinPreviousEditBlock();
	}
	insertion.setPosition(insertion.position() + e->replacementStart());
	insertion.setPosition(
		insertion.position() + e->replacementLength(),
		QTextCursor::KeepAnchor);
	if (open
		&& (removal.position() < insertion.selectionStart()
			|| removal.position() > insertion.selectionEnd())) {
		removal.endEditBlock();
		if (!alive) {
			return true;
		}
		insertion.joinPreviousEditBlock();
	}
	insertion.insertText(commit);
	insertion.endEditBlock();
	if (!alive) {
		return true;
	} else if (!e->preeditString().isEmpty()
		|| !attributes.isEmpty()
		|| !Composition(edit).isEmpty()) {
		auto copy = QInputMethodEvent(e->preeditString(), attributes);
		forward(&copy);
	} else {
		auto cursor = edit->textCursor();
		cursor.setPosition(cursor.position(), QTextCursor::KeepAnchor);
		// Qt's handler leaves the vertical-movement x at the caret.
		edit->setTextCursor(cursor);
	}
	return true;
}

} // namespace Ui
