#pragma once
// The sentence the caret is in, for focus mode: ICU's sentence breaks (as
// NEO uses Intl.Segmenter), the caret at a sentence's very end still in it,
// and the trailing space left off so the focus hugs the words.

#include <QList>
#include <QPair>
#include <QString>

namespace neosea {

QPair<int, int> sentenceAt(const QString &text, int offset, const QString &lang);
// every sentence of a paragraph, as spans, from an offset on; blank ones left out
QList<QPair<int, int>> sentenceSpans(const QString &text, int from, const QString &lang);

} // namespace neosea
