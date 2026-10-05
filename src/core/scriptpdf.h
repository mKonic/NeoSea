#pragma once
// A script as the industry prints it: Courier Prime 12 pt on US letter, the
// elements at their margins, 54 lines a page, a page number top right from
// page 2, (CONT'D) where the same voice comes back, and a title page.

#include "core/screenplay.h"

#include <QByteArray>
#include <QList>

namespace neosea::sp {

struct PrintLine {
    QString type;
    QList<Run> runs;
    bool contd = false;
};

// The script's paragraphs as lines to print: their runs (no marks), and
// (CONT'D) worked out
QList<PrintLine> printLines(const QList<Para> &paras);
// How many lines each element takes, set in Courier Prime at its width
QList<int> measureLines(const QList<PrintLine> &lines);
QByteArray buildScriptPdf(const QList<PrintLine> &lines, const TitlePage &title);

} // namespace neosea::sp
