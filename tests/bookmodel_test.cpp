#include "core/bookmodel.h"
#include "core/i18n.h"

#include "print.h"

#include <QJsonArray>

#include <gtest/gtest.h>

using namespace neosea;

namespace {

QJsonObject bookOf(QStringList order, QJsonObject kinds = {}, QJsonObject titles = {})
{
    QJsonObject b{{"chapterOrder", QJsonArray::fromStringList(order)}};
    if (!kinds.isEmpty()) b.insert("chapterKinds", kinds);
    if (!titles.isEmpty()) b.insert("chapterTitles", titles);
    return b;
}

struct English : ::testing::Test {
    void SetUp() override { I18n::setLocale("en", {}, {}); }
};

} // namespace

TEST_F(English, NumbersCountChaptersOnly)
{
    const auto b = bookOf({"pro", "a", "p1", "b", "c"}, {{"pro", "prologue"}, {"p1", "part"}});
    EXPECT_EQ(chapterName("pro", b), "Prologue");
    EXPECT_EQ(chapterName("a", b), "Chapter 1");
    EXPECT_EQ(chapterName("p1", b), "Part I");
    EXPECT_EQ(chapterName("c", b), "Chapter 3");
    EXPECT_EQ(chapterMark("p1", b), "I");
    EXPECT_EQ(chapterMark("pro", b), "❦");
    EXPECT_EQ(numberedChapters(b), 3);
}

TEST_F(English, RestartNumberingAtEachPart)
{
    auto b = bookOf({"p1", "a", "b", "p2", "c"}, {{"p1", "part"}, {"p2", "part"}});
    b.insert("restartNumbering", true);
    EXPECT_EQ(chapterName("c", b), "Chapter 1");
    EXPECT_EQ(chapterName("b", b), "Chapter 2");
    EXPECT_EQ(chapterName("p2", b), "Part II");
    EXPECT_EQ(numberedChapters(b, "a"), 2);
    EXPECT_EQ(numberedChapters(b, "c"), 1);
    EXPECT_EQ(numberedChapters(b), 3);
}

TEST_F(English, HeadingsAndUnnumbered)
{
    const auto b = bookOf({"a", "u"}, {{"u", "unnumbered"}}, {{"a", " The Start "}, {"u", "Interlude"}});
    EXPECT_EQ(chapterHeading("a", b), "Chapter 1 — The Start");
    EXPECT_EQ(chapterHeading("a", b, true), "The Start");
    EXPECT_EQ(chapterHeading("u", b), "Interlude");
    EXPECT_EQ(chapterName("u", b), "Interlude");
    EXPECT_EQ(numberedChapters(b), 1);
}

TEST_F(English, SoloStoryHasNoHeading)
{
    EXPECT_EQ(soloStory(bookOf({"a"})), "a");
    EXPECT_EQ(soloStory(bookOf({"d", "a"}, {{"d", "dedication"}})), "a"); // pages don't count
    EXPECT_EQ(soloStory(bookOf({"a", "b"})), "");
    EXPECT_EQ(soloStory(bookOf({"p", "a"}, {{"p", "prologue"}})), "");
}

TEST_F(English, LegacyRolesBecomeKinds)
{
    auto b = bookOf({"a", "b", "c"});
    b.insert("prologue", "a");
    b.insert("epilogue", "b"); // not last: no longer an epilogue
    EXPECT_EQ(chapterKind("a", b), "prologue");
    EXPECT_EQ(chapterKind("b", b), "chapter");
    EXPECT_TRUE(settleChapterKinds(b));
    EXPECT_FALSE(b.contains("prologue"));
    EXPECT_FALSE(b.contains("epilogue"));
    EXPECT_EQ(b.value("chapterKinds").toObject(), (QJsonObject{{"a", "prologue"}}));
    EXPECT_FALSE(settleChapterKinds(b));
}

TEST_F(English, SettleDropsKindsOfGoneEntries)
{
    auto b = bookOf({"a"}, {{"gone", "part"}, {"a", "bogus"}});
    EXPECT_TRUE(settleChapterKinds(b));
    EXPECT_TRUE(b.value("chapterKinds").toObject().isEmpty());
}

TEST(Roman, Numerals)
{
    EXPECT_EQ(roman(1), "I");
    EXPECT_EQ(roman(4), "IV");
    EXPECT_EQ(roman(14), "XIV");
    EXPECT_EQ(roman(1994), "MCMXCIV");
}

TEST(Words, CountsWordsWithLetters)
{
    EXPECT_EQ(countWords(""), 0);
    EXPECT_EQ(countWords("  Hello, world  "), 2);
    EXPECT_EQ(countWords("She paused — then « oui » !"), 4); // dashes and guillemets aren't words
    EXPECT_EQ(countWords("one two"), 2);
    EXPECT_EQ(countWords("3 apples"), 2);
}

TEST(Words, ThaiIsSegmented)
{
    // "I love you" in Thai, no spaces: more than one word
    EXPECT_GE(countWords(QString::fromUtf8("ฉันรักคุณ")), 2);
}
