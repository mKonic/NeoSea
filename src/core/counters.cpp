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

} // namespace neosea::counters
