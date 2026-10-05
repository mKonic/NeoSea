#include "core/booksession.h"
#include "core/bookmodel.h"
#include "core/i18n.h"

#include "print.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QThread>

#include <gtest/gtest.h>

using namespace neosea;

namespace {

// A library with one book of two chapters, opened by this device and by a
// second session standing in for the other device.
struct Synced : ::testing::Test {
    QTemporaryDir tmp;
    std::unique_ptr<Library> lib;
    QString id;

    void SetUp() override
    {
        I18n::setLocale("en", {}, {});
        lib = std::make_unique<Library>(tmp.path());
        QJsonObject b = lib->createBook("Sync", "Me");
        id = b.value("id").toString();
        lib->writeChapter(id, "c1", "<p>One two three.</p>");
        lib->writeChapter(id, "c2", "<p>Second chapter.</p>");
        b.insert("chapterOrder", QJsonArray{"c1", "c2"});
        lib->writeBookMeta(id, b);
    }

    // another device writes a chapter: a different size, so the stamp moves
    void otherDeviceWrites(const QString &chId, const QString &html) { lib->writeChapter(id, chId, html); }
};

} // namespace

TEST(Session, MetaSigIgnoresBookkeepingAndEmptyDefaults)
{
    QJsonObject a{{"title", "X"}, {"chapterOrder", QJsonArray{"c1"}}};
    QJsonObject b = a;
    b.insert("modified", "2026");
    b.insert("lastPosition", QJsonObject{{"chapterId", "c1"}});
    b.insert("chapterTitles", QJsonObject{});
    b.insert("subtitle", "");
    EXPECT_EQ(metaSig(a), metaSig(b));
    b.insert("subtitle", "New");
    EXPECT_NE(metaSig(a), metaSig(b));
}

TEST(Session, OnlyDropsMeansAnOlderCopy)
{
    EXPECT_TRUE(onlyDrops("<p>a b c</p>", "<p>a b</p>"));
    EXPECT_FALSE(onlyDrops("<p>a b</p>", "<p>a b c</p>"));
    EXPECT_FALSE(onlyDrops("<p>a b</p>", "<p>a x</p>"));
    EXPECT_FALSE(onlyDrops("<p>a</p>", "<p>a</p>"));
}

TEST_F(Synced, WritesOnlyWhatChanged)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    EXPECT_FALSE(s.chapterDirty("c1"));
    const auto before = lib->chapterStamps(id);
    s.flush(); // nothing changed: nothing written
    EXPECT_EQ(lib->chapterStamps(id), before);
    s.setChapterHtml("c2", "<p>Second chapter, edited.</p>");
    EXPECT_TRUE(s.chapterDirty("c2"));
    s.flush();
    EXPECT_FALSE(s.chapterDirty("c2"));
    EXPECT_EQ(lib->readChapter(id, "c2"), "<p>Second chapter, edited.</p>");
    EXPECT_EQ(lib->chapterStamps(id).value("c1"), before.value("c1"));
}

TEST_F(Synced, AdoptsTheOtherDevicesEdit)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    s.refresh("from other device"); // first look records the stamps
    otherDeviceWrites("c1", "<p>One two three, and four.</p>");
    const RefreshResult r = s.refresh("from other device");
    EXPECT_EQ(r.adopted, 1);
    EXPECT_EQ(r.conflicts, 0);
    EXPECT_EQ(s.chapterHtml("c1"), "<p>One two three, and four.</p>");
    EXPECT_FALSE(s.chapterDirty("c1"));
}

TEST_F(Synced, AnOlderCopyPutsThePageInDarlings)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    s.refresh("x");
    otherDeviceWrites("c1", "<p>One two</p>"); // every word the page has, fewer of them
    const RefreshResult r = s.refresh("x");
    EXPECT_EQ(r.adopted, 1);
    EXPECT_EQ(r.displaced, 1);
    ASSERT_EQ(s.darlings().size(), 1);
    EXPECT_EQ(s.darlings()[0].toObject().value("html").toString(), "<p>One two three.</p>");
    EXPECT_EQ(lib->readJson(id, "darlings", QJsonArray{}).toArray().size(), 1); // saved
}

TEST_F(Synced, BothChangedKeepsBothAsTwoChapters)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    s.refresh("from other device");
    s.setChapterHtml("c1", "<p>Mine, typed here.</p>");
    otherDeviceWrites("c1", "<p>Theirs, typed there.</p>");
    const RefreshResult r = s.refresh("from other device");
    EXPECT_EQ(r.conflicts, 1);
    const QStringList o = s.order();
    ASSERT_EQ(o.size(), 3);
    EXPECT_EQ(o[0], "c1");
    EXPECT_EQ(s.chapterHtml("c1"), "<p>Mine, typed here.</p>");
    EXPECT_EQ(s.chapterHtml(o[1]), "<p>Theirs, typed there.</p>");
    EXPECT_EQ(chapterTitle(o[1], s.book()), "from other device");
    // both reached the disk
    EXPECT_EQ(lib->readChapter(id, "c1"), "<p>Mine, typed here.</p>");
    EXPECT_EQ(lib->readChapter(id, o[1]), "<p>Theirs, typed there.</p>");
    EXPECT_EQ(chapterOrder(*lib->readBookMeta(id)).size(), 3);
}

TEST_F(Synced, AnEmptyReadNeverWipesWords)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    s.refresh("x");
    otherDeviceWrites("c1", "");
    const RefreshResult r = s.refresh("x");
    EXPECT_EQ(r.adopted, 0);
    EXPECT_EQ(s.chapterHtml("c1"), "<p>One two three.</p>");
}

TEST_F(Synced, NewChaptersFromTheOtherDeviceArrive)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    // the other device adds a chapter and saves book.json
    BookSession other(*lib, id);
    ASSERT_TRUE(other.load());
    other.createChapterAt(2, "<p>Third, from there.</p>");
    const RefreshResult r = s.refresh("x");
    EXPECT_TRUE(r.restructured);
    ASSERT_EQ(s.order().size(), 3);
    EXPECT_EQ(s.chapterHtml(s.order()[2]), "<p>Third, from there.</p>");
}

TEST_F(Synced, TheirNewStructureKeepsAChapterWithUnsavedWordsHere)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    s.setChapterHtml("c2", "<p>Unsaved words here.</p>");
    // the other device deletes c2 from the book
    BookSession other(*lib, id);
    ASSERT_TRUE(other.load());
    other.setOrder({"c1"});
    other.saveMeta();
    const RefreshResult r = s.refresh("x");
    EXPECT_TRUE(r.restructured);
    EXPECT_EQ(s.order(), (QStringList{"c1", "c2"}));
    EXPECT_EQ(s.chapterHtml("c2"), "<p>Unsaved words here.</p>");
}

TEST_F(Synced, StructuralUndoBringsBackADeletedChapter)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    s.snapshot("chapter delete");
    s.deleteChapter("c2");
    EXPECT_EQ(s.order(), QStringList{"c1"});
    EXPECT_TRUE(lib->readChapter(id, "c2").isEmpty());
    auto snap = s.undoStructure();
    ASSERT_TRUE(snap);
    EXPECT_EQ(snap->label, "chapter delete");
    EXPECT_EQ(s.order(), (QStringList{"c1", "c2"}));
    EXPECT_EQ(lib->readChapter(id, "c2"), "<p>Second chapter.</p>");
    EXPECT_FALSE(s.canUndoStructure());
}

TEST_F(Synced, UndoIsTenDeep)
{
    BookSession s(*lib, id);
    ASSERT_TRUE(s.load());
    for (int i = 0; i < 15; ++i) s.snapshot(QString::number(i));
    int n = 0;
    while (s.undoStructure()) n++;
    EXPECT_EQ(n, 10);
}
