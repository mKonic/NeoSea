#include "core/editops.h"

#include "core/textdoc.h"

#include <QTextBlock>
#include <QTextDocument>

namespace neosea::edit {

namespace {

QStringList cls(const QTextBlock &b) { return doc::classes(b); }

bool has(const QTextBlock &b, const QString &c) { return cls(b).contains(c); }

void setCls(QTextBlock b, QStringList c)
{
    c.removeDuplicates();
    doc::setClasses(b, c);
}

void addClass(QTextBlock b, const QString &c)
{
    QStringList l = cls(b);
    l << c;
    setCls(b, l);
}

void removeClasses(QTextBlock b, const QStringList &gone)
{
    QStringList l = cls(b);
    for (const QString &g : gone) l.removeAll(g);
    setCls(b, l);
}

// the block's whole text, its format kept
void setBlockText(QTextBlock b, const QString &text)
{
    QTextCursor c(b);
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    c.insertText(text, QTextCharFormat());
}

bool allItalic(const QTextBlock &b)
{
    bool any = false;
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        if (!f.isValid() || f.text().trimmed().isEmpty()) continue;
        any = true;
        if (!f.charFormat().fontItalic()) return false;
    }
    return any;
}

void makeBreak(QTextBlock b)
{
    resetParagraph(b, {"scene-break"});
    setBlockText(b, "***");
}

} // namespace

bool isBlank(const QTextBlock &b)
{
    if (!b.isValid()) return false;
    if (!doc::blockText(b).trimmed().isEmpty()) return false;
    for (auto it = b.begin(); !it.atEnd(); ++it)
        if (it.fragment().charFormat().hasProperty(doc::MarkSid)) return false;
    return true;
}

void italicize(QTextBlock b, QTextCursor *typing)
{
    QTextCursor c(b);
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCharFormat f;
    f.setFontItalic(true);
    if (c.hasSelection()) c.mergeCharFormat(f);
    QTextCursor bc(b);
    bc.mergeBlockCharFormat(f);
    if (typing && typing->block() == b) typing->mergeCharFormat(f);
}

void romanize(QTextBlock b, QTextCursor *typing)
{
    // only the default italic a poem was born with comes off; a word the
    // writer set in italic inside prose was never the poem's
    if (!allItalic(b) && !isBlank(b)) return;
    QTextCursor c(b);
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCharFormat f;
    f.setFontItalic(false);
    if (c.hasSelection()) c.mergeCharFormat(f);
    QTextCursor bc(b);
    bc.mergeBlockCharFormat(f);
    if (typing && typing->block() == b) typing->mergeCharFormat(f);
}

void resetParagraph(QTextBlock b, const QStringList &classes)
{
    QTextBlockFormat f = b.blockFormat();
    f.setProperty(doc::ParaAttrs, QStringList{});
    f.setProperty(doc::ParaClass, classes.join(' '));
    f.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
    QTextCursor(b).setBlockFormat(f);
}

void removeParagraph(QTextBlock b)
{
    if (!b.isValid()) return;
    QTextDocument *d = const_cast<QTextDocument *>(b.document());
    QTextBlock prev = b.previous(), next = b.next();
    QTextCursor c(d);
    if (prev.isValid()) {
        // from the end of the paragraph above to the end of this one
        c.setPosition(prev.position() + prev.length() - 1);
        c.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    } else if (next.isValid()) {
        // the first paragraph: the next one moves up and keeps its own format
        const QTextBlockFormat nf = next.blockFormat();
        const QTextCharFormat ncf = next.charFormat();
        c.setPosition(b.position());
        c.setPosition(next.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setBlockFormat(nf);
        c.setBlockCharFormat(ncf);
    } else {
        setBlockText(b, QString());
        resetParagraph(b, {});
    }
}

QString textBefore(const QTextCursor &c)
{
    return c.block().text().left(c.positionInBlock());
}

Result enter(QTextCursor &c, const Context &ctx)
{
    if (c.hasSelection()) return Result::NotHandled;
    QTextBlock block = c.block();
    // on a page (a dedication, a part) Enter is only a new line: breaks and
    // chapters belong to the story
    if (!ctx.story && !has(block, "poetry")) return Result::NotHandled;
    if (has(block, "scene-break")) return Result::Handled; // Enter on a *** line: nothing
    // Enter in a poem or a flush paragraph steps back into prose: an empty
    // line becomes ordinary in place; otherwise the new paragraph is plain
    if (has(block, "poetry") || has(block, "flush")) {
        if (isBlank(block)) {
            ctx.snapshot("poetry paragraph to prose", false);
            removeClasses(block, {"poetry", "flush"});
            romanize(block, &c);
            return Result::Structural;
        }
        const bool wasPoetry = has(block, "poetry");
        c.insertBlock();
        removeClasses(c.block(), {"poetry", "flush"});
        if (wasPoetry) romanize(c.block(), &c);
        return Result::Handled;
    }
    QTextBlock prev = block.previous();
    if (!isBlank(block)) {
        const bool atStart = c.positionInBlock() == 0;
        // the second or third Enter mid-flow: the caret sits at the start of
        // the text the previous press pushed down
        if (atStart && ctx.enterRun >= 2 && prev.isValid()) {
            if (has(prev, "scene-break")) {
                ctx.snapshot("chapter split", false);
                removeParagraph(prev);
                return Result::SplitChapter;
            }
            ctx.snapshot("section break", ctx.enterRun >= 2);
            if (isBlank(prev)) {
                // a break carries nothing over from the paragraph it was made in
                makeBreak(prev);
                c.setPosition(block.position());
            } else {
                QTextCursor at(block);
                at.insertBlock();
                makeBreak(at.block().previous());
                // the caret stays at the start of the words the break pushed down
                c.setPosition(at.position());
            }
            return Result::Structural;
        }
        return Result::NotHandled;
    }
    // the third Enter at the end of the flow: an empty line under a *** splits the chapter
    if (prev.isValid() && has(prev, "scene-break")) {
        ctx.snapshot("chapter split", false);
        removeParagraph(prev);
        return Result::SplitChapter;
    }
    // the second: the empty line becomes the break, and a fresh line follows
    if (prev.isValid()) {
        ctx.snapshot("section break", ctx.enterRun >= 2);
        makeBreak(block);
        QTextCursor end(block);
        end.movePosition(QTextCursor::EndOfBlock);
        QTextBlockFormat fresh;
        fresh.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
        end.insertBlock(fresh, QTextCharFormat());
        c.setPosition(end.position());
        c.setCharFormat(QTextCharFormat());
        return Result::Structural;
    }
    return Result::NotHandled;
}

Result shiftEnter(QTextCursor &c, const Context &ctx)
{
    if (c.hasSelection()) return Result::NotHandled;
    QTextBlock block = c.block();
    if (has(block, "scene-break")) return Result::Handled;
    if (has(block, "flush")) {
        c.insertBlock();
        addClass(c.block(), "flush");
        return Result::Handled;
    }
    if (isBlank(block) || c.positionInBlock() == 0) {
        ctx.snapshot("flush paragraph", false);
        addClass(block, "flush");
        return Result::Structural;
    }
    c.insertBlock();
    addClass(c.block(), "flush");
    return Result::Handled;
}

Result poetryEnter(QTextCursor &c, const Context &ctx)
{
    if (c.hasSelection()) return Result::NotHandled;
    QTextBlock block = c.block();
    if (has(block, "scene-break")) return Result::Handled;
    if (has(block, "poetry")) {
        // another line of the poem
        c.insertBlock();
        addClass(c.block(), "poetry");
        if (isBlank(c.block())) italicize(c.block(), &c);
        return Result::Handled;
    }
    ctx.snapshot("poetry paragraph", false);
    if (isBlank(block) || c.positionInBlock() == 0) {
        removeClasses(block, {"flush"});
        addClass(block, "poetry");
        italicize(block, &c);
        c.setPosition(block.position());
        return Result::Structural;
    }
    c.insertBlock();
    resetParagraph(c.block(), {"poetry"});
    italicize(c.block(), &c);
    return Result::Structural;
}

Result backspace(QTextCursor &c, const Context &ctx)
{
    if (c.hasSelection()) return Result::NotHandled;
    QTextBlock block = c.block();
    if (c.positionInBlock() != 0) return Result::NotHandled;
    // the start of a poem or a flush paragraph: prose again
    if (has(block, "poetry") || has(block, "flush")) {
        ctx.snapshot("poetry paragraph to prose", false);
        if (has(block, "poetry")) romanize(block, &c);
        removeClasses(block, {"poetry", "flush"});
        return Result::Structural;
    }
    QTextBlock prev = block.previous();
    // just below a ***: the break goes, never the prose into it
    if (prev.isValid() && has(prev, "scene-break")) {
        ctx.snapshot("section break removed", false);
        removeParagraph(prev);
        return Result::Structural;
    }
    // at the chapter's very first letter?
    bool allBlankAbove = true;
    for (QTextBlock b = block.previous(); b.isValid(); b = b.previous())
        if (!b.text().isEmpty()) { allBlankAbove = false; break; }
    if (!allBlankAbove) return Result::NotHandled;
    bool empty = true;
    for (QTextBlock b = c.document()->begin(); b.isValid(); b = b.next())
        if (!b.text().trimmed().isEmpty()) { empty = false; break; } // ghosts count as content
    if (empty) return Result::EmptyChapter;
    if (prev.isValid()) {
        // an empty line above the first words: it goes, and the words move up
        if (has(prev, "ghost")) return Result::NotHandled;
        ctx.snapshot("empty line removed", false);
        removeParagraph(prev);
        return Result::Structural;
    }
    return Result::ChapterStart;
}

Result forwardDelete(QTextCursor &c, const Context &ctx)
{
    if (c.hasSelection()) return Result::NotHandled;
    QTextBlock block = c.block();
    if (c.positionInBlock() != block.length() - 1) return Result::NotHandled;
    QTextBlock next = block.next();
    if (next.isValid() && has(next, "scene-break")) {
        ctx.snapshot("section break removed", false);
        removeParagraph(next);
        return Result::Structural;
    }
    return Result::NotHandled;
}

void toggleParaKind(QTextCursor &c, const QString &kind, const Context &ctx)
{
    QTextDocument *d = c.document();
    QTextBlock first = d->findBlock(c.selectionStart()), last = d->findBlock(c.selectionEnd());
    QList<QTextBlock> blocks;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        if (!has(b, "scene-break")) blocks << b;
        if (b == last) break;
    }
    if (blocks.isEmpty()) return;
    ctx.snapshot(kind + " paragraph", false);
    bool all = true;
    for (const QTextBlock &b : blocks) all &= has(b, kind);
    const bool on = !all;
    c.beginEditBlock();
    for (QTextBlock b : blocks) {
        const bool wasPoetry = has(b, "poetry");
        removeClasses(b, {"poetry", "flush"});
        if (on) addClass(b, kind);
        if (kind == "poetry" && on) italicize(b);
        else if (wasPoetry) romanize(b);
    }
    c.endEditBlock();
    c.setPosition(blocks.first().position());
}

void setAlignment(QTextCursor &c, Qt::Alignment a)
{
    QTextDocument *d = c.document();
    QTextBlock first = d->findBlock(c.selectionStart()), last = d->findBlock(c.selectionEnd());
    c.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        if (!has(b, "scene-break")) {
            QTextBlockFormat f = b.blockFormat();
            f.setAlignment(a == Qt::AlignLeft ? Qt::AlignLeft | Qt::AlignAbsolute : a);
            QTextCursor(b).setBlockFormat(f);
        }
        if (b == last) break;
    }
    c.endEditBlock();
}

// ---------------------------------------------------------------------------

namespace {

// swaps the n characters before the cursor for text, keeping their format
void replaceBefore(QTextCursor &c, int n, const QString &text)
{
    c.setPosition(c.position() - n, QTextCursor::KeepAnchor);
    QTextCharFormat f = c.charFormat();
    c.insertText(text, f);
}

bool inPoetryOrBreak(const QTextBlock &b)
{
    const QStringList l = doc::classes(b);
    return l.contains("poetry") || l.contains("scene-break") || l.contains("sp-paren");
}

} // namespace

void dashBeforeEnter(QTextCursor &c, const TypingContext &ctx)
{
    if (c.hasSelection()) return;
    const QStringList l = doc::classes(c.block());
    if (l.contains("sp-heading") || l.contains("sp-character") || l.contains("sp-transition") || l.contains("sp-shot")) return;
    const QString before = textBefore(c);
    auto e = typing::dashForKey(before, QString(), typing::dashStyle(ctx.language), ctx.manuscript);
    if (!e) return;
    const int at = c.block().position() + int(e->at);
    QTextCursor r(c.document());
    r.setPosition(at);
    r.setPosition(at + int(e->from.size()), QTextCursor::KeepAnchor);
    r.insertText(e->to, r.charFormat());
}

bool typeKey(QTextCursor &c, const QString &key, const TypingContext &ctx, Undoable *undo)
{
    if (undo) *undo = Undoable{};
    if (key.size() != 1 || c.hasSelection()) return false;
    const QChar k = key[0];
    QTextBlock block = c.block();
    const QStringList l = doc::classes(block);
    const bool scriptName = l.contains("sp-heading") || l.contains("sp-character") || l.contains("sp-transition") || l.contains("sp-shot");

    // the dialogue dash: a hyphen turns once the key after it shows what it is
    if (!scriptName) {
        const QString before = textBefore(c);
        if (auto e = typing::dashForKey(before, key, typing::dashStyle(ctx.language), ctx.manuscript)) {
            const int at = block.position() + int(e->at);
            QTextCursor r(c.document());
            r.setPosition(at);
            r.setPosition(at + int(e->from.size()), QTextCursor::KeepAnchor);
            r.insertText(e->to, r.charFormat());
            if (undo) *undo = Undoable{Undoable::Dash, at, e->from, e->to, key, 1};
        }
    }

    QString before = textBefore(c);
    // capitals as you type, in the manuscript
    if (ctx.manuscript && !inPoetryOrBreak(block)) {
        if (auto cap = typing::autoCapital(before, key, ctx.language)) {
            const int at = c.position();
            c.insertText(*cap);
            if (undo) *undo = Undoable{Undoable::Capital, at, key, *cap, QString(), 1};
            return true;
        }
        if (auto i = typing::englishI(before, key, ctx.language)) {
            const int at = block.position() + int(*i);
            QTextCursor r(c.document());
            r.setPosition(at);
            r.setPosition(at + 1, QTextCursor::KeepAnchor);
            r.insertText("I", r.charFormat());
            if (undo) *undo = Undoable{Undoable::Capital, at, "i", "I", key, 1};
            before = textBefore(c);
        }
    }

    // Markdown emphasis: the closing mark sets the words between in italic or bold
    if (ctx.markdown && (k == '*' || k == '_' || k == '~')) {
        if (k == '~') {
            if (auto at = typing::mdStrikeMatch(before)) {
                const int base = block.position();
                const int end = base + int(before.size());
                const int open = base + int(*at);
                c.beginEditBlock();
                QTextCursor r(c.document());
                r.setPosition(end - 1);
                r.setPosition(end, QTextCursor::KeepAnchor);
                r.removeSelectedText();
                r.setPosition(open);
                r.setPosition(open + 2, QTextCursor::KeepAnchor);
                r.removeSelectedText();
                r.setPosition(open);
                r.setPosition(end - 3, QTextCursor::KeepAnchor);
                QTextCharFormat f;
                f.setFontStrikeOut(true);
                r.mergeCharFormat(f);
                c.endEditBlock();
                c.setPosition(end - 3);
                QTextCharFormat plain = c.charFormat();
                plain.setFontStrikeOut(false);
                c.setCharFormat(plain);
                if (undo) *undo = Undoable{Undoable::Emphasis, open, before.mid(int(*at)) + key, {}, key, 1};
                return true;
            }
        } else if (auto hit = typing::mdEmphasisMatch(before, k)) {
            const int base = block.position();
            const int end = base + int(before.size());
            const int start = base + int(hit->start);
            c.beginEditBlock();
            QTextCursor r(c.document());
            if (hit->part) {
                r.setPosition(end - hit->part);
                r.setPosition(end, QTextCursor::KeepAnchor);
                r.removeSelectedText();
            }
            r.setPosition(start);
            r.setPosition(start + hit->open, QTextCursor::KeepAnchor);
            r.removeSelectedText();
            const int innerEnd = end - hit->part - hit->open;
            r.setPosition(start);
            r.setPosition(innerEnd, QTextCursor::KeepAnchor);
            QTextCharFormat f;
            if (hit->italic) f.setFontItalic(true);
            if (hit->bold) f.setFontWeight(QFont::Bold);
            r.mergeCharFormat(f);
            c.endEditBlock();
            c.setPosition(innerEnd);
            // what comes next is typed plain again
            QTextCharFormat plain = c.charFormat();
            if (hit->italic) plain.setFontItalic(false);
            if (hit->bold) plain.setFontWeight(QFont::Normal);
            c.setCharFormat(plain);
            if (undo) *undo = Undoable{Undoable::Emphasis, start, before.mid(hit->start) + key, {}, key, 1};
            return true;
        }
    }

    const QString prev1 = before.right(1);
    if (k == '-' && prev1 == "-") {
        replaceBefore(c, 1, QStringLiteral("—"));
        return true;
    }
    if (k == '.' && before.endsWith("..")) {
        replaceBefore(c, 2, QStringLiteral("…"));
        return true;
    }
    // French: a narrow no-break space before ; : ! ? (Quebec: the colon only)
    const QString french = typing::frenchTypography(ctx.language, ctx.uiLocale);
    if (!french.isEmpty()) {
        const bool spaced = french == "ca" ? k == ':' : QStringLiteral(";:!?").contains(k);
        if (spaced && (prev1 == " " || prev1 == QStringLiteral(" "))) {
            replaceBefore(c, 1, QStringLiteral(" ") + key);
            return true;
        }
    }
    if (k == '"' || k == '\'') {
        const typing::QuoteStyle q = k == '"' ? ctx.doubleQuotes : typing::quoteStyle(ctx.language);
        c.insertText(typing::curlQuote(before, k, q));
        return true;
    }
    return false;
}

void revert(QTextCursor &c, const Undoable &u)
{
    switch (u.kind) {
    case Undoable::None: return;
    case Undoable::Dash:
    case Undoable::Capital: {
        QTextCursor r(c.document());
        r.setPosition(u.at);
        r.setPosition(u.at + int(u.to.size()), QTextCursor::KeepAnchor);
        r.insertText(u.was, r.charFormat());
        c.setPosition(u.at + int(u.was.size()) + int(u.key.size()));
        return;
    }
    case Undoable::Emphasis: {
        // the styling goes and the marks come back as typed
        c.document()->undo(&c);
        QTextCursor r(c.document());
        r.setPosition(u.at);
        r.setPosition(u.at + int(u.was.size()) - 1, QTextCursor::KeepAnchor);
        if (r.selectedText() == u.was.left(u.was.size() - 1)) {
            r.clearSelection();
            r.setPosition(u.at + int(u.was.size()) - 1);
            r.insertText(u.key);
            c.setPosition(r.position());
        }
        return;
    }
    }
}

} // namespace neosea::edit
