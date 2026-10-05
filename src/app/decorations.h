#pragma once
// What the screen lays over a page's text (search matches, spelling
// squiggles), kept in layers so one doesn't wipe the other out: a page's
// extra selections are all its layers together.

#include <QTextEdit>

namespace neosea::deco {

void set(QTextEdit *e, const QString &layer, const QList<QTextEdit::ExtraSelection> &marks);

} // namespace neosea::deco
