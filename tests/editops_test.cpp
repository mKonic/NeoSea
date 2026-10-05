#include "core/editops.h"
#include "core/textdoc.h"

#include "print.h"

#include <QTextDocument>

#include <gtest/gtest.h>

using namespace neosea;
using edit::Result;

namespace {

// a chapter with a caret, and the keys a writer presses
struct Page {
    QTextDocument d;
    QTextCursor c{&d};
    int enterRun = 0;
    QStringList snaps;
    edit::Context ctx;

    explicit Page(const QString &html)
    {
        doc::loadHtml(d, html);
        c.movePosition(QTextCursor::End);
        ctx.snapshot = [this](const QString &label, bool) { snaps << label; };
    }
    void type(const QString &s)
    {
        enterRun = 0;
        for (QChar k : s) {
            edit::TypingContext tc;
            tc.doubleQuotes = typing::quoteStyle("en");
            if (!edit::typeKey(c, QString(k), tc, nullptr)) c.insertText(QString(k));
        }
    }
    Result enter()
    {
        ctx.enterRun = ++enterRun;
        const Result r = edit::enter(c, ctx);
        if (r == Result::NotHandled) c.insertBlock();
        return r;
    }
    QString html() const { return doc::html(d); }
};

} // namespace

TEST(Enter, OnceIsANewParagraph)
{
    Page p("<p>Words</p>");
    EXPECT_EQ(p.enter(), Result::NotHandled);
    p.type("More");
    EXPECT_EQ(p.html(), "<p>Words</p><p>More</p>");
}

TEST(Enter, TwiceIsASectionBreakThriceIsAChapter)
{
    Page p("<p>Words</p>");
    p.enter();
    EXPECT_EQ(p.enter(), Result::Structural);
    EXPECT_EQ(p.html(), "<p>Words</p><p class=\"scene-break\">***</p><p><br></p>");
    EXPECT_EQ(p.snaps, QStringList{"section break"});
    EXPECT_EQ(p.enter(), Result::SplitChapter);
    EXPECT_EQ(p.html(), "<p>Words</p><p><br></p>"); // the break gives way to the split
    EXPECT_EQ(p.c.block().blockNumber(), 1);
}

TEST(Enter, MidSentenceTheRhythmStillWorks)
{
    Page p("<p>The first half. The second half.</p>");
    p.c.setPosition(16); // after "The first half. "
    p.enter();           // the paragraph splits
    EXPECT_EQ(p.enter(), Result::Structural);
    EXPECT_EQ(p.html(), "<p>The first half. </p><p class=\"scene-break\">***</p><p>The second half.</p>");
    EXPECT_EQ(p.enter(), Result::SplitChapter);
    EXPECT_EQ(p.c.block().text(), "The second half.");
}

TEST(Enter, ABreakCarriesNoAttributesOver)
{
    Page p("<p style=\"text-align: justify;\" data-sec-id=\"s1\">Words</p>");
    p.enter();
    p.enter();
    EXPECT_EQ(p.html(), "<p style=\"text-align: justify;\" data-sec-id=\"s1\">Words</p><p class=\"scene-break\">***</p><p><br></p>");
}

TEST(Enter, PoetryAndFlush)
{
    Page p("<p>Prose</p>");
    p.ctx.enterRun = 1;
    EXPECT_EQ(edit::poetryEnter(p.c, p.ctx), Result::Structural);
    p.type("a line");
    EXPECT_EQ(edit::poetryEnter(p.c, p.ctx), Result::Handled); // ⇧Enter in a poem: its next line
    p.type("and another");
    EXPECT_EQ(p.html(), "<p>Prose</p><p class=\"poetry\"><i>a line</i></p><p class=\"poetry\"><i>and another</i></p>");
    // Enter leaves the poem: plain prose again
    p.enterRun = 0;
    p.enter();
    p.type("Back");
    EXPECT_EQ(p.html(), "<p>Prose</p><p class=\"poetry\"><i>a line</i></p><p class=\"poetry\"><i>and another</i></p><p>Back</p>");
    EXPECT_EQ(edit::shiftEnter(p.c, p.ctx), Result::Handled);
    p.type("Flush");
    EXPECT_TRUE(p.html().endsWith("<p>Back</p><p class=\"flush\">Flush</p>")) << p.html().toStdString();
}

TEST(Backspace, RemovesTheBreakNotTheProse)
{
    Page p("<p>One</p><p class=\"scene-break\">***</p><p>Two</p>");
    p.c.setPosition(p.d.findBlockByNumber(2).position());
    EXPECT_EQ(edit::backspace(p.c, p.ctx), Result::Structural);
    EXPECT_EQ(p.html(), "<p>One</p><p>Two</p>");
}

TEST(Backspace, PoetryBecomesProseFirst)
{
    Page p("<p>One</p><p class=\"poetry\"><i>verse</i></p>");
    p.c.setPosition(p.d.findBlockByNumber(1).position());
    EXPECT_EQ(edit::backspace(p.c, p.ctx), Result::Structural);
    EXPECT_EQ(p.html(), "<p>One</p><p>verse</p>");
    EXPECT_EQ(edit::backspace(p.c, p.ctx), Result::NotHandled); // the second merges up as usual
}

TEST(Backspace, AtTheChaptersStart)
{
    Page empty("<p><br></p>");
    empty.c.setPosition(0);
    EXPECT_EQ(edit::backspace(empty.c, empty.ctx), Result::EmptyChapter);
    Page start("<p>Words</p>");
    start.c.setPosition(0);
    EXPECT_EQ(edit::backspace(start.c, start.ctx), Result::ChapterStart);
    Page lines("<p><br></p><p>Words</p>");
    lines.c.setPosition(lines.d.findBlockByNumber(1).position());
    EXPECT_EQ(edit::backspace(lines.c, lines.ctx), Result::Structural); // the empty line above goes
    EXPECT_EQ(lines.html(), "<p>Words</p>");
    Page ghost("<p class=\"ghost\">note</p>");
    ghost.c.setPosition(0);
    EXPECT_EQ(edit::backspace(ghost.c, ghost.ctx), Result::ChapterStart); // ghosts count as content
}

TEST(Typing, SmartKeysInTheDocument)
{
    Page p("<p><br></p>");
    p.type("she said--\"it's...\" and i left");
    EXPECT_EQ(p.c.block().text(), "She said—“it’s…” and I left");
}

TEST(Typing, MarkdownSetsItalicAndTypesPlainAfter)
{
    Page p("<p><br></p>");
    p.type("A *word* here");
    EXPECT_EQ(p.html(), "<p>A <i>word</i> here</p>");
    Page b("<p><br></p>");
    b.type("A **big** one");
    EXPECT_EQ(b.html(), "<p>A <b>big</b> one</p>");
}

TEST(Typing, RevertBringsBackWhatWasTyped)
{
    Page p("<p><br></p>");
    edit::TypingContext tc;
    edit::Undoable u;
    p.c.insertText("Hello. ");
    ASSERT_TRUE(edit::typeKey(p.c, "w", tc, &u));
    EXPECT_EQ(p.c.block().text(), "Hello. W");
    edit::revert(p.c, u);
    EXPECT_EQ(p.c.block().text(), "Hello. w");
}

TEST(Format, ToggleAndAlign)
{
    Page p("<p>a</p><p class=\"scene-break\">***</p><p>b</p>");
    p.c.setPosition(0);
    p.c.setPosition(p.d.characterCount() - 1, QTextCursor::KeepAnchor);
    edit::toggleParaKind(p.c, "poetry", p.ctx);
    EXPECT_EQ(p.html(), "<p class=\"poetry\"><i>a</i></p><p class=\"scene-break\">***</p><p class=\"poetry\"><i>b</i></p>");
    // the caret lands at the first paragraph's start; select them all again
    p.c.setPosition(0);
    p.c.setPosition(p.d.characterCount() - 1, QTextCursor::KeepAnchor);
    edit::toggleParaKind(p.c, "poetry", p.ctx);
    EXPECT_EQ(p.html(), "<p>a</p><p class=\"scene-break\">***</p><p>b</p>");
    p.c.setPosition(0);
    edit::setAlignment(p.c, Qt::AlignHCenter);
    EXPECT_EQ(p.html(), "<p style=\"text-align: center;\">a</p><p class=\"scene-break\">***</p><p>b</p>");
}

TEST(Breaks, TypingNeverLandsOnAStarLine)
{
    Page p("<p class=\"scene-break\">***</p><p>Words</p>");
    p.c.setPosition(0);
    ASSERT_TRUE(edit::stepOffBreak(p.c));
    p.c.insertText("Above");
    EXPECT_EQ(p.html(), "<p>Above</p><p class=\"scene-break\">***</p><p>Words</p>");
    Page q("<p>One</p><p class=\"scene-break\">***</p><p>Two</p>");
    q.c.setPosition(q.d.findBlockByNumber(1).position() + 2);
    ASSERT_TRUE(edit::stepOffBreak(q.c));
    q.c.insertText("Mid");
    EXPECT_EQ(q.html(), "<p>One</p><p class=\"scene-break\">***</p><p>Mid</p><p>Two</p>");
}

TEST(Breaks, ProseMergedIntoABreakIsHealed)
{
    QTextDocument d;
    doc::loadHtml(d, "<p class=\"scene-break\">***</p><p class=\"scene-break\">Words***</p>");
    EXPECT_EQ(edit::healBreaks(d), 1);
    EXPECT_EQ(doc::html(d), "<p class=\"scene-break\">***</p><p>Words***</p>");
}
