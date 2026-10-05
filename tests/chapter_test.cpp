#include "core/chapter.h"
#include "core/htmldom.h"

#include "print.h"

#include <gtest/gtest.h>

using namespace neosea;

TEST(Html, ParsesForgivingly)
{
    auto root = html::parse("<p>a &amp; b&nbsp;c<p>second<br>line</p><script>evil()</script>&copy;");
    ASSERT_EQ(root->children.size(), 3);
    EXPECT_EQ(root->children[0]->textContent(), QString("a & b c"));
    EXPECT_EQ(root->children[1]->children.size(), 3);
    EXPECT_EQ(root->children[2]->text, "©");
}

TEST(Html, SerializesLikeInnerHtml)
{
    const QString src = "<p class=\"x\" data-sec-id=\"s&quot;1\">a &lt;b&gt; &amp;&nbsp;c<br></p>";
    EXPECT_EQ(html::innerHtml(*html::parse(src)), src);
}

TEST(Html, StrayLessThanIsText)
{
    EXPECT_EQ(html::parse("2 < 3")->textContent(), "2 < 3");
}

TEST(Chapter, RoundTripsNeoMarkupUnchanged)
{
    const QString src = "<p>Plain <i>italic</i> and <b><i>both</i></b>.</p>"
                        "<p class=\"scene-break\">***</p>"
                        "<p class=\"poetry\"><i>A line of verse</i></p>"
                        "<p class=\"flush\" style=\"text-align: center;\">Centered</p>"
                        "<p>Mark<span class=\"ph-mark\" data-sid=\"s1\" contenteditable=\"false\">⚑</span> here</p>"
                        "<p><br></p>";
    EXPECT_EQ(serializeChapter(parseChapter(src)), src);
}

TEST(Chapter, ScreenOnlyMarksAreNeverKept)
{
    const auto paras = parseChapter("<p data-first=\"\" data-pg=\"2\" class=\"sp-dialogue\">Hi</p>");
    EXPECT_EQ(serializeChapter(paras), "<p class=\"sp-dialogue\">Hi</p>");
}

TEST(Chapter, JunkSpansUnwrapAndStylesCount)
{
    const auto paras = parseChapter("<p><span style=\"text-indent: 2em\">a <span style=\"font-weight: 700\">b</span></span>"
                                    "<span class=\"darling-anchor\"></span></p>");
    ASSERT_EQ(paras.size(), 1);
    ASSERT_EQ(paras[0].runs.size(), 2);
    EXPECT_EQ(paras[0].runs[0].text, "a ");
    EXPECT_TRUE(paras[0].runs[1].b);
    EXPECT_EQ(serializeChapter(paras), "<p>a <b>b</b></p>");
}

TEST(Chapter, LooseTextAndDivsBecomeParagraphs)
{
    const auto paras = parseChapter("loose <i>words</i><div>a div</div><br><div><p>nested</p></div>");
    ASSERT_EQ(paras.size(), 3);
    EXPECT_EQ(paras[0].text(), "loose words");
    EXPECT_EQ(paras[1].text(), "a div");
    EXPECT_EQ(paras[2].text(), "nested");
}

TEST(Chapter, LineBreakInsideAParagraph)
{
    const auto paras = parseChapter("<p>one<br>two</p><p>end<br></p>");
    EXPECT_EQ(paras[0].text(), QString("one two"));
    EXPECT_EQ(paras[1].text(), "end");
    EXPECT_EQ(serializeChapter(paras), "<p>one<br>two</p><p>end</p>");
}

TEST(Chapter, AlignmentReadsAndWrites)
{
    Para p = parseChapter("<p style=\"text-align: right;\">x</p>")[0];
    EXPECT_EQ(p.align(), "right");
    p.setAlign("left");
    EXPECT_FALSE(p.hasAttr("style"));
    p.setAlign("justify");
    EXPECT_EQ(p.attr("style"), "text-align: justify;");
}

TEST(Chapter, PlainTextSkipsGhostsAndMarks)
{
    EXPECT_EQ(chapterPlainText("<p>end.</p><p class=\"ghost\" data-sec-id=\"a\">note</p>"
                               "<p>Next<span class=\"ph-mark\" data-sid=\"1\" contenteditable=\"false\">⚑</span></p>"),
              "end.\nNext\n");
}

TEST(Paste, WordAndDocsHtml)
{
    const auto paras = cleanPaste(
        "<b style=\"font-weight:normal\" id=\"docs-internal\"><p><span style=\"font-style:italic\">Hello</span>"
        "<span>  world </span></p><p><span style=\"font-weight:700\">Bold</span><br>next</p></b>"
        "<p>&nbsp;</p><img src=x><table><tr><td>gone</td></tr></table>");
    ASSERT_EQ(paras.size(), 3);
    EXPECT_EQ(serializeChapter(paras), "<p><i>Hello</i> world</p><p><b>Bold</b></p><p>next</p>");
}
