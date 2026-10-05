#include "app/decorations.h"

#include <QHash>
#include <QMap>

namespace neosea::deco {

void set(QTextEdit *e, const QString &layer, const QList<QTextEdit::ExtraSelection> &marks)
{
    static QHash<QTextEdit *, QMap<QString, QList<QTextEdit::ExtraSelection>>> layers;
    if (!layers.contains(e)) QObject::connect(e, &QObject::destroyed, [e] { layers.remove(e); });
    auto &mine = layers[e];
    if (marks.isEmpty()) mine.remove(layer);
    else mine.insert(layer, marks);
    QList<QTextEdit::ExtraSelection> all;
    for (const auto &l : mine) all += l;
    e->setExtraSelections(all);
}

} // namespace neosea::deco
