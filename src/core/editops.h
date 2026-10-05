#pragma once
// What the keys do on the page, as functions of a cursor in a chapter's
// document, so the rules are tested apart from any widget:
//
//   Enter            a new paragraph
//   Enter, Enter     a *** section break, wherever the caret is
//   Enter ×3         the chapter splits here (the caller makes the new chapter)
//   ⇧Enter           a flush paragraph (no indent); again for another
//   ⌘⇧Enter          a poetry paragraph, italic; ⇧Enter continues the poem
//   Backspace        at a break's edge removes the break; at the start of a
//                    poetry or flush paragraph makes it prose again
//
// Each structural change asks for a snapshot first (so ⌘Z can restore it)
// and reports itself, so the editor can clear the document's own undo, which
// never saw the change and would corrupt the page replaying over it.

#include "core/typing.h"

#include <QString>
#include <QTextBlock>
#include <QTextCursor>

#include <functional>

namespace neosea::edit {

using Snapshot = std::function<void(const QString &label, bool rejoin)>;

enum class Result {
    NotHandled,   // let the editor do its default
    Handled,      // done; ordinary typing, the document's undo still holds
    Structural,   // done; a structural change (clear the document's undo)
    SplitChapter, // the chapter splits at the cursor's block (the caller does it)
    EmptyChapter, // Backspace in a chapter with nothing in it (the caller deletes it)
    ChapterStart, // Backspace at the chapter's very first letter (the caller merges up)
};

struct Context {
    int enterRun = 0;       // Enters in a row so far, this one included
    bool story = true;      // a page a book carries has no breaks or chapters
    Snapshot snapshot = [](const QString &, bool) {};
};

Result enter(QTextCursor &c, const Context &ctx);
Result shiftEnter(QTextCursor &c, const Context &ctx);       // flush
Result poetryEnter(QTextCursor &c, const Context &ctx);      // ⌘⇧Enter, and ⇧Enter inside a poem
Result backspace(QTextCursor &c, const Context &ctx);        // breaks, poetry, flush, empty lines at the top
Result forwardDelete(QTextCursor &c, const Context &ctx);    // a break below

// Format → Poetry Paragraph / Flush Paragraph over every paragraph the
// selection touches (a paragraph is one or the other, or prose)
void toggleParaKind(QTextCursor &c, const QString &kind, const Context &ctx);
// Format → Align Paragraph
void setAlignment(QTextCursor &c, Qt::Alignment a);

// the paragraph's text before the cursor, as the typing rules read it
QString textBefore(const QTextCursor &c);

struct TypingContext {
    QString language = "en";      // the writing language
    QString uiLocale = "en";
    bool markdown = true;         // *italic* as you type
    bool manuscript = true;       // the opening dialogue dash is the manuscript's only
    typing::QuoteStyle doubleQuotes; // the book's (bookQuotes)
};

// What a typed key did, so ⌘Z right after can put back what was typed
struct Undoable {
    enum Kind { None, Dash, Capital, Emphasis } kind = None;
    int at = 0;          // document position of the change
    QString was, to;     // the text before and after
    QString key;         // the key that went on after it
    int steps = 0;       // document undo steps the change took
};

// The smart keys: dashes, ellipses, quotes, French spacing, capitals and
// Markdown emphasis. key is the typed text. Returns true when it typed the
// key itself (the editor must not insert it again).
bool typeKey(QTextCursor &c, const QString &key, const TypingContext &ctx, Undoable *undo);
// The dialogue dash on Enter ("Espere -" then Enter): before the paragraph ends
void dashBeforeEnter(QTextCursor &c, const TypingContext &ctx);
// ⌘Z right after a smart key: what was typed comes back as typed
void revert(QTextCursor &c, const Undoable &u);

// paragraph-level helpers the editor shares
bool isBlank(const QTextBlock &b);
void italicize(QTextBlock b, QTextCursor *typing = nullptr);
void romanize(QTextBlock b, QTextCursor *typing = nullptr);
// a paragraph made fresh: these classes, no other attributes, aligned left
void resetParagraph(QTextBlock b, const QStringList &classes);
// removes a paragraph whole, leaving its neighbours' formats as they were
void removeParagraph(QTextBlock b);
// Words typed on a *** line go on a line of their own: before it at its
// start, after it anywhere else. Moves the cursor there; true if it did.
bool stepOffBreak(QTextCursor &c);
// A *** line holding anything but *** is prose that got merged into a
// break: it becomes prose again. Returns how many were healed.
int healBreaks(QTextDocument &d);

} // namespace neosea::edit
