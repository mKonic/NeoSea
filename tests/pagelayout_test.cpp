#include "core/fonts.h"
#include "core/pagelayout.h"
#include "core/textdoc.h"

#include "print.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <gtest/gtest.h>

using namespace neosea;

namespace {

struct Page {
    QTextDocument d;
    PageLayout *layout;

    explicit Page(const QString &html, PageStyle s = {})
    {
        registerBundledFonts();
        layout = new PageLayout(&d);
        d.setDocumentLayout(layout);
        QFont f("Gelasio");
        f.setPixelSize(17);
        d.setDefaultFont(f);
        d.setTextWidth(600);
        doc::loadHtml(d, html);
        if (s.dropCapFamily.isEmpty()) s.dropCapFamily = "Libre Bodoni";
        layout->setStyle(s);
    }
    QTextBlock block(int i) { return d.findBlockByNumber(i); }
    QTextLine line(int b, int l) { return block(b).layout()->lineAt(l); }
};

const QString kLong = "It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of "
                      "foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of Light.";

} // namespace

TEST(TextDoc, RoundTripsThroughTheDocument)
{
    const QString src = "<p>Plain <i>italic</i> and <b><i>both</i></b> <u>u</u> <strike>s</strike></p>"
                        "<p class=\"scene-break\">***</p>"
                        "<p class=\"ghost\" data-sec-id=\"s1\">a note</p>"
                        "<p style=\"text-align: center;\">Centered</p>"
                        "<p>Mark<span class=\"ph-mark\" data-sid=\"x1\" contenteditable=\"false\">⚑</span> here<br>next</p>"
                        "<p><br></p>";
    QTextDocument d;
    doc::loadHtml(d, src);
    EXPECT_EQ(d.blockCount(), 6);
    EXPECT_EQ(doc::html(d), src);
    EXPECT_TRUE(doc::hasClass(d.findBlockByNumber(1), "scene-break"));
    EXPECT_EQ(doc::attr(d.findBlockByNumber(2), "data-sec-id"), "s1");
    EXPECT_EQ(doc::blockText(d.findBlockByNumber(4)), QString("Mark here next"));
}

TEST(TextDoc, TypingKeepsTheParagraphsClass)
{
    QTextDocument d;
    doc::loadHtml(d, "<p class=\"poetry\"><i>verse</i></p>");
    QTextCursor c(&d);
    c.movePosition(QTextCursor::End);
    c.insertBlock(); // Enter in a poem keeps the class and the italic, as NEO's next line of verse does
    c.insertText("more");
    EXPECT_EQ(doc::html(d), "<p class=\"poetry\"><i>verse</i></p><p class=\"poetry\"><i>more</i></p>");
    doc::setClasses(d.findBlockByNumber(1), {});
    doc::setAttr(d.findBlockByNumber(1), "data-sec-id", "z");
    EXPECT_EQ(doc::html(d), "<p class=\"poetry\"><i>verse</i></p><p data-sec-id=\"z\"><i>more</i></p>");
}

TEST(Layout, OpeningParagraphHasTheDropCapAndNoIndent)
{
    Page p("<p>" + kLong + "</p><p>" + kLong + "</p>");
    EXPECT_EQ(p.layout->openingBlock(), 0);
    const QRectF cap = p.layout->dropCapRect();
    ASSERT_TRUE(cap.isValid());
    // the first two lines wrap around the cap; the third runs the full width
    EXPECT_NEAR(p.line(0, 0).x(), cap.width(), 0.5);
    EXPECT_NEAR(p.line(0, 1).x(), cap.width(), 0.5);
    ASSERT_GE(p.block(0).layout()->lineCount(), 3);
    EXPECT_NEAR(p.line(0, 2).x(), 0, 0.5);
    // the next paragraph is indented two ems
    EXPECT_NEAR(p.line(1, 0).x(), 34, 0.5);
    EXPECT_NEAR(p.line(1, 1).x(), 0, 0.5);
}

TEST(Layout, SpeechOpensWithoutACapAndKeepsItsIndent)
{
    Page p("<p>— Hello, she said.</p>");
    EXPECT_FALSE(p.layout->dropCapRect().isValid());
    EXPECT_NEAR(p.line(0, 0).x(), 34, 0.5);
}

TEST(Layout, BreaksPoetryAndGhosts)
{
    Page p("<p>Opening words.</p><p class=\"scene-break\">***</p><p>After the break.</p>"
           "<p class=\"poetry\"><i>a line</i></p><p class=\"ghost\">note</p><p>Indented again.</p>");
    // after a break: no indent
    EXPECT_NEAR(p.line(2, 0).x(), 0, 0.5);
    // the break is centered
    const QTextLine brk = p.line(1, 0);
    EXPECT_NEAR(brk.x() + brk.width() / 2 + 0, 300, 1);
    EXPECT_GT(brk.naturalTextWidth(), 0);
    EXPECT_NEAR(brk.x() + (brk.width() - brk.naturalTextWidth()) / 2, 300 - brk.naturalTextWidth() / 2, 30);
    // poetry pulled in two and a half ems
    EXPECT_NEAR(p.line(3, 0).x(), 2.5 * 17, 0.5);
    // a ghost sits flush
    EXPECT_NEAR(p.line(4, 0).x(), 0, 0.5);
    EXPECT_NEAR(p.line(5, 0).x(), 34, 0.5);
}

TEST(Layout, NoDropCapWhenTurnedOff)
{
    PageStyle s;
    s.dropCapFamily = QString();
    Page p("<p>" + kLong + "</p>", s);
    p.layout->setStyle(s); // Page fills a default family in; switch it off again
    EXPECT_FALSE(p.layout->dropCapRect().isValid());
    EXPECT_NEAR(p.line(0, 0).x(), 0, 0.5);
}

TEST(Layout, HitTestFindsTheLetter)
{
    Page p("<p>" + kLong + "</p><p>Second paragraph here.</p>");
    const QRectF second = p.layout->blockBoundingRect(p.block(1));
    const int pos = p.layout->hitTest(QPointF(34 + 1, second.center().y()), Qt::FuzzyHit);
    EXPECT_EQ(pos, p.block(1).position());
    // clicking the drop cap lands at the paragraph's start
    EXPECT_EQ(p.layout->hitTest(p.layout->dropCapRect().center(), Qt::FuzzyHit), 0);
    // past the end of the document: the last paragraph
    EXPECT_GE(p.layout->hitTest(QPointF(10, 1e6), Qt::FuzzyHit), p.block(1).position());
}

TEST(Layout, ScriptElementsAtTheirMargins)
{
    PageStyle s;
    s.script = true;
    s.fontPx = 10;
    Page p("<p class=\"sp-heading\">int. galley</p><p class=\"sp-character\">kim</p><p class=\"sp-dialogue\">Now.</p>", s);
    EXPECT_NEAR(p.line(1, 0).x(), 132, 0.5);
    EXPECT_NEAR(p.line(2, 0).x(), 60, 0.5);
    // two blank lines over a heading, one over a character, none over dialogue
    const qreal h1 = p.layout->blockBoundingRect(p.block(1)).top();
    const qreal h2 = p.layout->blockBoundingRect(p.block(2)).top();
    EXPECT_NEAR(h2 - h1, 2 * 10, 0.5); // the name's blank line and the name itself
}

TEST(Layout, TheWalkingNoteGetsRoomUnderItsParagraph)
{
    Page p("<p>One.</p><p>Two.</p><p>Three.</p>");
    const qreal before = p.layout->blockBoundingRect(p.block(2)).top();
    p.layout->setSpaceAfter(1, 40);
    EXPECT_NEAR(p.layout->blockBoundingRect(p.block(2)).top(), before + 40, 0.5);
    p.layout->setSpaceAfter(-1, 0);
    EXPECT_NEAR(p.layout->blockBoundingRect(p.block(2)).top(), before, 0.5);
}
