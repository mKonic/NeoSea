#include "core/counters.h"

#include "print.h"

#include <gtest/gtest.h>

using namespace neosea;

TEST(Counters, TheWritingDayRollsOverWhenTheWriterSays)
{
    const QDateTime late(QDate(2026, 10, 6), QTime(1, 30));
    EXPECT_EQ(counters::writingDay(late, 0), "2026-10-06");
    EXPECT_EQ(counters::writingDay(late, 3), "2026-10-05"); // still last night's session
}

TEST(Counters, TodaysWordsAndCuts)
{
    QJsonObject book;
    bool changed = false;
    EXPECT_EQ(counters::trackDaily(book, 1000, "d1", &changed), 0);
    EXPECT_TRUE(changed); // a day began
    EXPECT_EQ(counters::trackDaily(book, 1250, "d1", &changed), 250);
    EXPECT_FALSE(changed);
    // cutting below the day's start moves the start: today never reads below zero
    EXPECT_EQ(counters::trackDaily(book, 900, "d1", &changed), 0);
    EXPECT_TRUE(changed);
    EXPECT_EQ(counters::trackDaily(book, 950, "d1"), 50);
    EXPECT_EQ(counters::trackDaily(book, 950, "d2"), 0); // a new day starts from the total
    EXPECT_EQ(book.value("dailyCounts").toObject().size(), 2);
}

TEST(Counters, Pages)
{
    EXPECT_EQ(counters::pageCount(0), 1);
    EXPECT_EQ(counters::pageCount(250), 1);
    EXPECT_EQ(counters::pageCount(251), 2);
}

TEST(Counters, ThirtyDaysForTheChart)
{
    QJsonObject book;
    book.insert("dailyCounts", QJsonObject{{"2026-10-01", QJsonObject{{"start", 100}, {"end", 400}}},
                                           {"2026-10-03", QJsonObject{{"start", 400}, {"end", 350}}},
                                           {"2026-10-05", QJsonObject{{"start", 350}, {"end", 900}}}});
    const auto c = counters::lastThirtyDays(book, QDateTime(QDate(2026, 10, 5), QTime(15, 0)), 0);
    ASSERT_EQ(c.days.size(), 30);
    EXPECT_EQ(c.days.last(), "2026-10-05");
    EXPECT_EQ(c.days.first(), "2026-09-06");
    EXPECT_EQ(c.daily.last(), 550);
    EXPECT_EQ(c.daily[c.days.indexOf("2026-10-03")], 0); // a day that cut more than it wrote shows nothing
    EXPECT_EQ(c.total.first(), 100);                     // before the first day written: where it began
    EXPECT_EQ(c.total[c.days.indexOf("2026-10-02")], 400); // carried over a day with no writing
    EXPECT_EQ(c.total.last(), 900);
}

TEST(Counters, ASprintStartsLowerWhenWordsAreCut)
{
    counters::Sprint s{500, 1000, 0, false};
    EXPECT_EQ(s.words(1100), 100);
    EXPECT_EQ(s.words(900), 0);
    EXPECT_EQ(s.words(950), 50);
}
