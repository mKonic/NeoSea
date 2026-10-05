#pragma once
// The sentence the caret is in, for focus mode: ICU's sentence breaks (as
// NEO uses Intl.Segmenter), the caret at a sentence's very end still in it,
// and the trailing space left off so the focus hugs the words.

#include <QPair>
#include <QString>

namespace neosea {

QPair<int, int> sentenceAt(const QString &text, int offset, const QString &lang);

} // namespace neosea
