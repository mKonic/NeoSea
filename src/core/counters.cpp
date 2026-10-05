#include "core/counters.h"

namespace neosea::counters {

QString writingDay(const QDateTime &now, int dayEndsAt)
{
    QDate d = now.date();
    if (now.time().hour() < dayEndsAt) d = d.addDays(-1);
    return d.toString("yyyy-MM-dd");
}

int trackDaily(QJsonObject &book, int total, const QString &today, bool *changed)
{
    QJsonObject daily = book.value("dailyCounts").toObject();
    QJsonObject day = daily.value(today).toObject();
    bool moved = false;
    if (day.isEmpty()) {
        day = QJsonObject{{"start", total}, {"end", total}};
        moved = true;
    } else if (day.value("end").toInt() != total) {
        day.insert("end", total);
    }
    if (total < day.value("start").toInt()) {
        day.insert("start", total);
        moved = true;
    }
    daily.insert(today, day);
    book.insert("dailyCounts", daily);
    if (changed) *changed = moved;
    return day.value("end").toInt() - day.value("start").toInt();
}

Chart lastThirtyDays(const QJsonObject &book, const QDateTime &now, int dayEndsAt)
{
    Chart c;
    const QJsonObject counts = book.value("dailyCounts").toObject();
    for (int i = 29; i >= 0; --i) c.days << writingDay(now.addDays(-i), dayEndsAt);
    int last = 0;
    for (const QString &d : c.days)
        if (counts.contains(d)) {
            last = counts.value(d).toObject().value("start").toInt();
            break;
        }
    for (const QString &d : c.days) {
        const QJsonObject day = counts.value(d).toObject();
        if (!day.isEmpty()) last = day.value("end").toInt();
        c.daily << (day.isEmpty() ? 0 : std::max(0, day.value("end").toInt() - day.value("start").toInt()));
        c.total << last;
    }
    return c;
}

} // namespace neosea::counters
