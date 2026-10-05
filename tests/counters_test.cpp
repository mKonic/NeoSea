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
