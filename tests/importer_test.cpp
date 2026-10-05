#include "core/importer.h"

#include "print.h"

#include <gtest/gtest.h>

using namespace neosea;

namespace {

QList<DocxPara> lines(std::initializer_list<const char *> l)
{
    QList<DocxPara> out;
    for (const char *s : l) out << DocxPara{QString::fromUtf8(s)};
    return out;
}

} // namespace

TEST(Import, HeadingsStartChaptersAndNumberingIsDropped)
{
    const auto r = chapterize("draft", lines({"Chapter 1", "It began.", "***", "Later.", "Chapter 2: The Road", "On.",
                                              "Part of me wanted to run."}));
    ASSERT_EQ(r.chapters.size(), 2);
    EXPECT_EQ(r.chapters[0].title, "");
    EXPECT_EQ(r.chapters[0].paras.size(), 3);
    EXPECT_TRUE(r.chapters[0].paras[1].scene);
    EXPECT_EQ(r.chapters[1].title, ""); // opens with a chapter word: NEO numbers it itself
    // a line that only opens with "Part" and reads as a sentence is prose
    EXPECT_EQ(r.chapters[1].paras.last().text, "Part of me wanted to run.");
}

TEST(Import, BareNumbersNeedALadder)
{
    auto one = chapterize("x", lines({"Seven.", "Seven was the number."}));
    EXPECT_EQ(one.chapters.size(), 1);
    EXPECT_EQ(one.chapters[0].paras[0].text, "Seven.");
    auto ladder = chapterize("x", lines({"1", "First.", "2", "Second.", "III", "Third."}));
    EXPECT_EQ(ladder.chapters.size(), 3);
}

TEST(Import, MarkdownHeadingsKeepTheirTitles)
{
    const auto r = chapterize("x", lines({"## The Silo", "Text.", "### Down", "More."}));
    ASSERT_EQ(r.chapters.size(), 2);
    EXPECT_EQ(r.chapters[0].title, "The Silo");
    EXPECT_EQ(r.chapters[1].title, "Down");
}

TEST(Import, TitleAndBylineGoToTheTitlePage)
{
    const auto r = chapterize("wool", lines({"WOOL", "by Hugh Howey", "The children were playing."}));
    EXPECT_EQ(r.title, "WOOL");
    EXPECT_EQ(r.author, "Hugh Howey");
    EXPECT_EQ(r.chapters[0].paras.size(), 1);
    // a sentence that opens with "Par" is prose, not a byline
    const auto fr = chapterize("x", lines({"Le Titre", "Par une nuit sombre, il partit."}));
    EXPECT_EQ(fr.author, "");
}

TEST(Import, PrologueOnlyFirstEpilogueOnlyLast)
{
    const auto r = chapterize("x", lines({"Prologue", "Before.", "Chapter 1", "Middle.", "Epilogue", "After."}));
    ASSERT_EQ(r.chapters.size(), 3);
    EXPECT_EQ(r.chapters[0].role, "prologue");
    EXPECT_EQ(r.chapters[2].role, "epilogue");
    const auto lone = chapterize("x", lines({"Epilogue", "Only this."}));
    EXPECT_EQ(lone.chapters[0].role, "");
}

TEST(Import, PageBreakConfettiFallsBackToHeadings)
{
    QList<DocxPara> paras;
    for (int i = 0; i < 10; ++i) {
        DocxPara p{QString("Short paragraph %1.").arg(i)};
        p.pageBreak = true;
        paras << p;
    }
    EXPECT_EQ(chapterize("x", paras).chapters.size(), 1);
}

TEST(Import, DocxRunsAndStyles)
{
    const QString styles = R"(<w:styles><w:style w:type="character" w:styleId="Emphasis"><w:rPr><w:i/></w:rPr></w:style>
<w:style w:type="character" w:styleId="Strong2"><w:basedOn w:val="Emphasis"/><w:rPr><w:b/></w:rPr></w:style></w:styles>)";
    const QString doc = R"(<w:body>
<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Chapter One</w:t></w:r></w:p>
<w:p><w:r><w:rPr><w:b/></w:rPr><w:t>one</w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>two</w:t></w:r><w:r><w:t xml:space="preserve"> and </w:t></w:r><w:r><w:rPr><w:rStyle w:val="Emphasis"/></w:rPr><w:t>styled</w:t></w:r><w:r><w:rPr><w:rStyle w:val="Emphasis"/><w:i w:val="0"/></w:rPr><w:t> off</w:t></w:r><w:r><w:rPr><w:rStyle w:val="Strong2"/></w:rPr><w:t>both</w:t></w:r></w:p>
<w:p><w:r><w:br w:type="page"/><w:t>A &amp; B</w:t></w:r></w:p>
</w:body>)";
    const auto paras = docxParagraphs(doc, styles);
    ASSERT_EQ(paras.size(), 3);
    EXPECT_TRUE(paras[0].heading);
    EXPECT_EQ(paras[1].text, "**onetwo** and *styled* off***both***");
    EXPECT_TRUE(paras[2].pageBreak);
    EXPECT_EQ(paras[2].text, "A & B");
}

TEST(Import, ChapterHtmlSetsDashesAndEmphasis)
{
    ImportChapter ch;
    ch.paras = {ImportPara{"- Hello, **bold** & *it*"}, ImportPara{{}, true}};
    EXPECT_EQ(importedChapterHtml(ch, typing::dashStyle("pt")),
              "<p>— Hello, <b>bold</b> &amp; <i>it</i></p><p class=\"scene-break\">***</p>");
    EXPECT_EQ(importedChapterHtml(ImportChapter{}, typing::dashStyle("en")), "<p><br></p>");
}

TEST(Import, PlainTextSplitsOnBlankLines)
{
    const auto p = textParagraphs("one\nline\n\n  two \r\n\r\nthree");
    ASSERT_EQ(p.size(), 3);
    EXPECT_EQ(p[0].text, "one line");
    EXPECT_EQ(p[1].text, "two");
}
