#include "core/darlings.h"
#include "core/textdoc.h"

#include "print.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <gtest/gtest.h>

using namespace neosea;
using namespace neosea::darlings;

namespace {

QTextCursor select(QTextDocument &d, const QString &what)
{
    QTextCursor c = d.find(what);
    EXPECT_TRUE(c.hasSelection()) << what.toStdString();
    return c;
}

} // namespace

TEST(Darlings, APassageWithinAParagraphStaysInline)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>He was <i>very</i> tired that day.</p><p>Next.</p>");
    QTextCursor c = select(d, "was very tired ");
    const QJsonObject o = cut(c, "c1", "Chapter 1");
    EXPECT_EQ(doc::html(d), "<p>He that day.</p><p>Next.</p>");
    EXPECT_EQ(o.value("html").toString(), "was <i>very</i> tired ");
    EXPECT_EQ(o.value("text").toString(), "was very tired ");
    EXPECT_EQ(o.value("anchorPrefix").toString(), "He ");
    EXPECT_EQ(o.value("anchorSuffix").toString(), "that day.Next."); // paragraphs end to end, as NEO counts them
    EXPECT_EQ(o.value("chapterId").toString(), "c1");
    EXPECT_TRUE(o.value("id").toString().startsWith("d-"));
}

TEST(Darlings, AParagraphTakenWholeLeavesNoEmptyLine)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>One.</p><p>Two two.</p><p>Three.</p>");
    QTextCursor c = select(d, "Two two.");
    const QJsonObject o = cut(c, "c1", "Chapter 1");
    EXPECT_EQ(doc::html(d), "<p>One.</p><p>Three.</p>");
    EXPECT_EQ(c.position(), 4); // the end of the paragraph before
    EXPECT_EQ(o.value("anchorPrefix").toString(), "One.");
    EXPECT_EQ(o.value("anchorSuffix").toString(), "Three.");
}

TEST(Darlings, SeveralParagraphsKeepTheirParagraphs)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>Keep.</p><p>Cut one.</p><p>Cut two.</p><p>Keep too.</p>");
    QTextCursor c(&d);
    c.setPosition(d.findBlockByNumber(1).position());
    c.setPosition(d.findBlockByNumber(2).position() + 8, QTextCursor::KeepAnchor);
    const QJsonObject o = cut(c, "c1", "Chapter 1");
    EXPECT_EQ(o.value("html").toString(), "<p>Cut one.</p><p>Cut two.</p>");
    EXPECT_EQ(o.value("text").toString(), "Cut one.\nCut two.");
    EXPECT_EQ(doc::html(d), "<p>Keep.</p><p>Keep too.</p>");
}

TEST(Darlings, FindingTheWayBack)
{
    const QString plain = "He that day.Next.";
    EXPECT_EQ(findPosition(plain, "He ", "that day."), 3);
    EXPECT_EQ(findPosition(plain, "He ", "gone"), 3);          // only the words before are left
    EXPECT_EQ(findPosition(plain, "gone", "Next."), 12);        // only the words after
    EXPECT_EQ(findPosition(plain, "gone", "also gone"), -1);
    EXPECT_EQ(findPosition(plain, QString(), QString()), -1);   // no anchors at all (a deleted chapter)
}

TEST(Darlings, PlainAndDocumentPositions)
{
    QTextDocument d;
    doc::loadHtml(d, "<p>ab</p><p>cd</p>");
    EXPECT_EQ(bodyPlain(d), "abcd");
    EXPECT_EQ(toDocPos(d, 3), 4); // past the separator after "ab"
    EXPECT_EQ(toPlainPos(d, 4), 3);
    EXPECT_EQ(toDocPos(d, 2), 2); // the end of "ab"
}
