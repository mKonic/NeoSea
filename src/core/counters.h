#pragma once
// Words written today. The writing day follows the writer's clock and rolls
// over at library.dayEndsAt (0 = midnight), so a session past midnight counts
// toward the night it began. book.dailyCounts keeps {start, end} per day.
// Cutting is writing too: words cut below where the day began move the day's
// start down with them, so today never reads below zero.

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace neosea::counters {

QString writingDay(const QDateTime &now, int dayEndsAt);
// Records the book's total; returns today's words. changed: the day's start
// moved or a day began (worth a book.json save).
int trackDaily(QJsonObject &book, int total, const QString &today, bool *changed = nullptr);
// Pages as a manuscript counts them: 250 words to a page
inline int pageCount(int words) { return std::max(1, (words + 249) / 250); }

// The goals room's chart: the last 30 writing days, oldest first, the words
// each day added and the book's total at each day's end (carried forward over
// days with no writing)
struct Chart {
    QStringList days;
    QList<int> daily, total;
};
Chart lastThirtyDays(const QJsonObject &book, const QDateTime &now, int dayEndsAt);

// A sprint: so many words from now. Cut below where it began and its start
// comes down too, as the day's does.
struct Sprint {
    int target = 500;
    int startCount = 0;
    qint64 startMs = 0;
    bool done = false;
    int words(int total)
    {
        if (total < startCount) startCount = total;
        return total - startCount;
    }
};

} // namespace neosea::counters
