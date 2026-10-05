#pragma once
// The small part of vim writers use to move around a page (View → Vim Keys).
// Words as vim counts them: letters (and digits, _ and apostrophes) are one
// kind, punctuation another; space isn't a word.

#include <QString>

namespace neosea::vim {

int charClass(QChar c); // 0 space, 1 letters, 2 punctuation

// How far w, b or e moves within the paragraph, given the text before and
// after the caret: the number of characters (back for b), or -1 when the
// move runs off the paragraph's end (w, e: on to the next paragraph; b: back
// to the one before).
int wordSteps(QChar key, const QString &before, const QString &after);

} // namespace neosea::vim
