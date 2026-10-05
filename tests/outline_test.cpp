#include "core/outline.h"
#include "core/i18n.h"

#include "print.h"

#include <QJsonArray>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace neosea;
using namespace neosea::outline;

namespace {

QList<Para> P(const QString &html) { return parseChapter(html); }
QString H(const QList<Para> &p) { return serializeChapter(p); }

struct Book : ::testing::Test {
    QTemporaryDir tmp;
    std::unique_ptr<Library> lib;
    std::unique_ptr<BookSession> s;
    QString id;

    void SetUp() override
    {
        I18n::setLocale("en", {}, {});
        lib = std::make_unique<Library>(tmp.path());
        QJsonObject b = lib->createBook("Outline", "Me");
        id = b.value("id").toString();
        lib->writeChapter(id, "c1", "<p>One.</p><p class=\"scene-break\">***</p><p>Two.</p>");
        lib->writeChapter(id, "c2", "<p>Three.</p>");
        b.insert("chapterOrder", QJsonArray{"c1", "c2"});
        lib->writeBookMeta(id, b);
        s = std::make_unique<BookSession>(*lib, id);
        ASSERT_TRUE(s->load());
    }
};

} // namespace

TEST(Outline, SegmentsCutAtBreaks)
{
    const auto segs = segments(P("<p>A</p><p class=\"scene-break\">***</p><p data-sec-id=\"s1\">B b</p><p data-sec-id=\"s1\">split</p>"
                                 "<p class=\"scene-break\">***</p><p class=\"ghost\" data-sec-id=\"s2\">note</p>"),
                               {{"s1", "one"}, {"s2", "two"}});
    ASSERT_EQ(segs.size(), 3);
    EXPECT_EQ(segs[0].brk, -1);
    EXPECT_EQ(segs[1].id, "s1");
    EXPECT_EQ(segs[1].words, 3);
    EXPECT_EQ(segs[1].first, "B b");
    EXPECT_EQ(segs[2].id, "s2");
    EXPECT_EQ(segs[2].words, 0); // a ghost isn't writing
}

TEST(Outline, GhostsComeAndGoWithTheirNotes)
{
    QList<Para> p = P("<p>Opening.</p>");
    syncGhosts(p, {{"s1", "They meet."}});
    EXPECT_EQ(H(p), "<p>Opening.</p><p class=\"scene-break\" data-sec-brk=\"s1\">***</p><p class=\"ghost\" data-sec-id=\"s1\">They meet.</p>");
    syncGhosts(p, {{"s1", "They meet again."}});
    EXPECT_TRUE(H(p).contains(">They meet again.</p>"));
    syncGhosts(p, {});
    EXPECT_EQ(H(p), "<p>Opening.</p>"); // the ghost and its *** leave together
    QList<Para> empty = P("<p><br></p>");
    syncGhosts(empty, {{"s1", "First."}});
    EXPECT_EQ(H(empty), "<p class=\"ghost\" data-sec-id=\"s1\">First.</p>");
}

TEST(Outline, ANewNoteGoesBeforeTheNextPlacedSection)
{
    QList<Para> p = P("<p>A</p><p class=\"scene-break\">***</p><p data-sec-id=\"s2\">B</p>");
    syncGhosts(p, {{"s1", "between"}, {"s2", "second"}});
    EXPECT_EQ(H(p), "<p>A</p><p class=\"scene-break\" data-sec-brk=\"s1\">***</p><p class=\"ghost\" data-sec-id=\"s1\">between</p>"
                    "<p class=\"scene-break\">***</p><p data-sec-id=\"s2\">B</p>");
}

TEST_F(Book, ANewCardIsAGhostAfterItsCard)
{
    Board b(*s);
    const QString sec = b.addSection("c1", 0, "Middle.");
    EXPECT_EQ(s->chapterHtml("c1"), "<p>One.</p><p class=\"scene-break\" data-sec-brk=\"" + sec + "\">***</p><p class=\"ghost\" data-sec-id=\"" + sec
                                        + "\">Middle.</p><p class=\"scene-break\">***</p><p>Two.</p>");
    EXPECT_EQ(b.notes("c1").size(), 1);
    EXPECT_EQ(lib->readChapter(id, "c1"), s->chapterHtml("c1")); // saved
}

TEST_F(Book, MovingASectionMovesTheWriting)
{
    Board b(*s);
    b.moveSection("c1", 1, "c2", -1); // "Two." to the end of chapter 2
    EXPECT_EQ(s->chapterHtml("c1"), "<p>One.</p>");
    EXPECT_EQ(s->chapterHtml("c2"), "<p>Three.</p><p class=\"scene-break\">***</p><p>Two.</p>");
    ASSERT_TRUE(s->undoStructure());
    EXPECT_EQ(s->chapterHtml("c1"), "<p>One.</p><p class=\"scene-break\">***</p><p>Two.</p>");
}

TEST_F(Book, MovingTheFirstSectionWithinItsChapter)
{
    Board b(*s);
    b.moveSection("c1", 0, "c1", -1); // "One." after "Two."
    EXPECT_EQ(s->chapterHtml("c1"), "<p>Two.</p><p class=\"scene-break\">***</p><p>One.</p>");
}

TEST_F(Book, ASectionBecomesAChapter)
{
    Board b(*s);
    const QString newId = b.sectionToChapter("c1", 1);
    EXPECT_EQ(s->order(), (QStringList{"c1", newId, "c2"}));
    EXPECT_EQ(s->chapterHtml("c1"), "<p>One.</p>");
    EXPECT_EQ(s->chapterHtml(newId), "<p>Two.</p>");
}

TEST_F(Book, AChapterJoinsAnother)
{
    Board b(*s);
    QJsonObject notes{{"c2", "The reunion"}};
    s->book().insert("chapterNotes", notes);
    auto sec = b.joinChapter("c2", "c1");
    ASSERT_TRUE(sec);
    EXPECT_EQ(s->order(), QStringList{"c1"});
    EXPECT_EQ(s->chapterHtml("c1"), "<p>One.</p><p class=\"scene-break\">***</p><p>Two.</p><p class=\"scene-break\" data-sec-brk=\"" + *sec
                                        + "\">***</p><p data-sec-id=\"" + *sec + "\">Three.</p>");
    EXPECT_EQ(b.notes("c1").first().text, "The reunion");
    EXPECT_TRUE(lib->readChapter(id, "c2").isEmpty()); // its file went after the words were safe
}

TEST_F(Book, LooseCardsComeAndGo)
{
    Board b(*s);
    const QString sec = b.addSection("c2", 0, "An idea");
    const int segIdx = int(b.segments("c2").size()) - 1;
    ASSERT_TRUE(b.sectionToLoose("c2", segIdx));
    EXPECT_EQ(s->chapterHtml("c2"), "<p>Three.</p>"); // its ghost left the page
    ASSERT_EQ(b.loose().size(), 1);
    const QString lid = b.loose()[0].toObject().value("id").toString();
    b.looseToSection(lid, "c1", -1);
    EXPECT_TRUE(b.loose().isEmpty());
    EXPECT_TRUE(s->chapterHtml("c1").endsWith("class=\"ghost\" data-sec-id=\"" + b.notes("c1").first().id + "\">An idea</p>"));
    EXPECT_FALSE(b.sectionToLoose("c1", 1)); // "Two." has writing: it stays
    (void)sec;
}

TEST_F(Book, ListShiftTabMakesTheSectionAChapter)
{
    Board b(*s);
    const QString a = b.addSection("c2", 0, "A");
    const QString bb = b.addSection("c2", 1, "B");
    const QString newId = b.sectionNoteToChapter("c2", a);
    EXPECT_EQ(s->book().value("chapterNotes").toObject().value(newId).toString(), "A");
    // B came along: nothing of it was written
    EXPECT_EQ(b.notes(newId).size(), 1);
    EXPECT_EQ(b.notes(newId).first().id, bb);
    EXPECT_EQ(s->chapterHtml("c2"), "<p>Three.</p>");
}
