#include "core/exporter.h"
#include "core/i18n.h"
#include "core/importer.h"

#include "print.h"

#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace neosea;

namespace {

struct ExportEnglish : ::testing::Test {
    void SetUp() override { I18n::setLocale("en", {}, {}); }
};

QJsonObject sampleBook(QHash<QString, QString> &chapters)
{
    QJsonObject book{{"id", "book-x"},
                     {"title", "No Wind"},
                     {"author", "Hugh Howey"},
                     {"chapterOrder", QJsonArray{"ded", "toc", "a", "p1", "b", "ack"}},
                     {"chapterKinds", QJsonObject{{"ded", "dedication"}, {"toc", "contents"}, {"p1", "part"}, {"ack", "acknowledgments"}}},
                     {"chapterTitles", QJsonObject{{"b", "The Bell"}}}};
    chapters["ded"] = "<p>For Amber</p>";
    chapters["toc"] = "";
    chapters["a"] = "<p>It <i>began</i>.</p><p class=\"scene-break\">***</p><p>— Then speech.</p>"
                    "<p class=\"ghost\" data-sec-id=\"s9\">a note</p><p class=\"scene-break\" data-sec-brk=\"s9\">***</p>";
    chapters["p1"] = "<p>The Deep</p><p>— an old saying</p>";
    chapters["b"] = "<p class=\"poetry\"><i>Ring the <i>bell</i></i></p><p>Done<span class=\"ph-mark\" data-sid=\"1\" contenteditable=\"false\">⚑</span>.</p>";
    chapters["ack"] = "<p>Thanks.</p>";
    return book;
}

ExportData sample()
{
    QHash<QString, QString> ch;
    QJsonObject book = sampleBook(ch);
    ExportData d;
    bookExportData(book, ch, false, "en", d);
    return d;
}

QString unzipText(const QByteArray &zip, const QString &entry)
{
    QTemporaryDir tmp;
    QFile f(tmp.filePath("x.zip"));
    (void)f.open(QIODevice::WriteOnly);
    f.write(zip);
    f.close();
    return QString::fromUtf8(zipEntry(tmp.filePath("x.zip"), entry));
}

} // namespace

TEST(Export, ParasDropGhostsMarksAndFlipNestedItalic)
{
    const auto paras = parasFromHtml("<p class=\"ghost\" data-sec-id=\"g\">note</p><p class=\"scene-break\" data-sec-brk=\"g\">***</p>"
                                     "<p class=\"poetry\"><i>Ring the <i>bell</i></i></p><p>x<span class=\"ph-mark\" data-sid=\"1\">⚑</span>&nbsp;y</p><p> </p>");
    ASSERT_EQ(paras.size(), 2);
    EXPECT_TRUE(paras[0].poetry);
    ASSERT_EQ(paras[0].runs.size(), 2);
    EXPECT_TRUE(paras[0].runs[0].i);
    EXPECT_FALSE(paras[0].runs[1].i); // emphasis inside a poem is set upright
    EXPECT_EQ(paras[1].text, "x y");
}

TEST_F(ExportEnglish, BookGathersSectionsAndContents)
{
    const ExportData d = sample();
    QStringList kinds;
    for (const Section &s : d.sections) kinds << s.kind + ":" + s.heading;
    EXPECT_EQ(kinds, (QStringList{"dedication:", "chapter:Chapter 1", "part:Part I", "chapter:Chapter 2 — The Bell",
                                  "acknowledgments:Acknowledgments"}));
    EXPECT_TRUE(d.contents);
    EXPECT_TRUE(d.sections[0].front);   // before the Contents entry
    EXPECT_FALSE(d.sections[1].front);
    EXPECT_EQ(d.sections[2].partTitle, "The Deep");
    EXPECT_EQ(d.sections[3].level, 1); // inside the part
    EXPECT_EQ(d.sections[1].paras.size(), 3); // the ghost and its planted break are gone
    QStringList toc;
    for (const auto &e : d.toc) toc << e.type + ":" + e.label;
    EXPECT_EQ(toc, (QStringList{"chapter:Chapter 1", "part:Part I: The Deep", "chapter:Chapter 2 — The Bell", "page:Acknowledgments"}));
    EXPECT_FALSE(d.uuid.isEmpty());
}

TEST_F(ExportEnglish, SoloStoryHasNoHeading)
{
    QJsonObject book{{"title", "Short"}, {"chapterOrder", QJsonArray{"a"}}};
    ExportData d;
    EXPECT_TRUE(bookExportData(book, {{"a", "<p>Once.</p>"}}, false, "en", d)); // a uuid was given
    EXPECT_EQ(d.sections.first().heading, "");
    EXPECT_EQ(d.toc.first().label, "Short");
}

TEST_F(ExportEnglish, PlainTextAndMarkdown)
{
    const ExportData d = sample();
    const QString txt = buildTxt(d);
    EXPECT_TRUE(txt.startsWith("NO WIND\nby Hugh Howey\n\n\n"));
    EXPECT_TRUE(txt.contains("CHAPTER 1\n\nIt began.\n\n\n***\n\n— Then speech.\n\n"));
    EXPECT_TRUE(txt.contains("PART I: THE DEEP"));
    EXPECT_TRUE(txt.contains("    Ring the bell\n"));
    const QString md = buildMd(d);
    EXPECT_TRUE(md.startsWith("# No Wind\n\n**by Hugh Howey**\n\n"));
    EXPECT_TRUE(md.contains("It *began*."));
    EXPECT_TRUE(md.contains("> *Ring the* bell")); // spaces stay outside the marks
}

TEST_F(ExportEnglish, WebPageMarksTheOpeningParagraph)
{
    const QString h = buildHtml(sample(), {});
    EXPECT_TRUE(h.contains("<p class=\"first\">It <i>began</i>.</p>"));
    EXPECT_TRUE(h.contains("<p class=\"dialogue\">— Then speech.</p>")); // speech after a break keeps its indent
    EXPECT_TRUE(h.contains("<nav class=\"contents\">"));
    // the contents follow the dedication
    EXPECT_LT(h.indexOf("class=\"page dedication\""), h.indexOf("<nav class=\"contents\">"));
    EXPECT_FALSE(h.contains("a note"));
}

TEST_F(ExportEnglish, DocxCarriesHeadingsAndRuns)
{
    const QByteArray docx = buildDocx(sample());
    const QString doc = unzipText(docx, "word/document.xml");
    ASSERT_FALSE(doc.isEmpty());
    EXPECT_TRUE(doc.contains("<w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t xml:space=\"preserve\">Chapter 1</w:t>"));
    EXPECT_TRUE(doc.contains("<w:rPr><w:i/></w:rPr><w:t xml:space=\"preserve\">began</w:t>"));
    EXPECT_TRUE(doc.contains("Heading2")); // the chapter in the part
    EXPECT_FALSE(unzipText(docx, "word/styles.xml").isEmpty());
}

TEST_F(ExportEnglish, EpubIsAPackage)
{
    CoverImage cover;
    cover.bytes = "fake";
    const QByteArray epub = buildEpub(sample(), cover);
    // the mimetype comes first, stored: an EPUB reader looks at byte 30
    EXPECT_EQ(epub.mid(30, 8), "mimetype");
    EXPECT_EQ(epub.mid(38, 20), "application/epub+zip");
    const QString opf = unzipText(epub, "OEBPS/content.opf");
    EXPECT_TRUE(opf.contains("<dc:title>No Wind</dc:title>"));
    EXPECT_TRUE(opf.contains("<itemref idref=\"ch1\"/>\n<itemref idref=\"nav\"/>")); // front, then contents
    const QString nav = unzipText(epub, "OEBPS/nav.xhtml");
    EXPECT_TRUE(nav.contains("Part I: The Deep</a>\n<ol>\n<li><a href=\"ch4.xhtml\">Chapter 2 — The Bell</a></li>"));
    EXPECT_TRUE(unzipText(epub, "OEBPS/ch2.xhtml").contains("<p class=\"first\">It <em>began</em>.</p>"));
}

TEST_F(ExportEnglish, PdfNumbersTheStoryAndFillsTheContents)
{
    if (QStandardPaths::findExecutable("pdftotext").isEmpty()) GTEST_SKIP() << "pdftotext not installed";
    QString err;
    const QByteArray pdf = buildPdf(sample(), {}, &err);
    ASSERT_TRUE(pdf.startsWith("%PDF")) << err.toStdString();
    QTemporaryDir tmp;
    QFile f(tmp.filePath("b.pdf"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(pdf);
    f.close();
    QProcess p;
    p.start("pdftotext", {"-layout", tmp.filePath("b.pdf"), "-"});
    ASSERT_TRUE(p.waitForFinished(20000));
    const QString text = QString::fromUtf8(p.readAllStandardOutput());
    const QStringList pages = text.split('\f');
    if (qEnvironmentVariableIsSet("NEOSEA_DUMP_PDF")) fprintf(stderr, "%s\n", qPrintable(text));
    // title, dedication, contents, chapter 1, part, chapter 2, acknowledgments
    ASSERT_GE(pages.size(), 7);
    EXPECT_TRUE(pages[0].contains("No Wind"));
    EXPECT_TRUE(pages[1].contains("For Amber"));
    EXPECT_TRUE(pages[2].contains("Contents", Qt::CaseInsensitive)); // set in small capitals
    EXPECT_TRUE(pages[3].contains("Chapter 1", Qt::CaseInsensitive));
    // the contents give chapter 1 the page it landed on
    EXPECT_TRUE(pages[2].contains(QRegularExpression("Chapter 1\\s+4\\b"))) << pages[2].toStdString();
    // a story page carries its number at the foot; the dedication doesn't
    EXPECT_TRUE(pages[3].trimmed().endsWith("4"));
    EXPECT_FALSE(pages[1].trimmed().endsWith("2"));
}

TEST(Export, SafeNames)
{
    EXPECT_EQ(safeName("Wool: Omnibus!"), "Wool-Omnibus");
    EXPECT_EQ(safeName("Война и мир"), "Война-и-мир");
}
