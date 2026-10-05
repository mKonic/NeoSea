#pragma once
// What each entry in a book is, and what it's called. Every entry is a chapter
// unless book.chapterKinds says otherwise: a page a published book carries, a
// part, a prologue or an epilogue. Chapters are numbered; parts are numbered on
// their own (roman); the rest go by their names. An unnumbered chapter goes by
// its title alone. Prologues, epilogues and chapters (numbered or not) are the
// story and count toward the words. book.restartNumbering starts the chapter
// count again at 1 after every part.
//
// The book is the book.json object itself, so fields this port doesn't know
// about survive a save untouched.

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace neosea {

inline const QStringList kChapterKinds{"copyright", "dedication", "epigraph", "contents", "prologue", "part",
                                       "chapter", "unnumbered", "epilogue", "acknowledgments", "about"};
inline const QStringList kStoryKinds{"chapter", "unnumbered", "prologue", "epilogue"};
// what comes after the story, where a new chapter never goes
inline const QStringList kBackKinds{"epilogue", "acknowledgments", "about"};

// Bound-shelf page books
inline const QStringList kPageFront{"copyright", "dedication", "epigraph"};
inline const QStringList kPageBack{"acknowledgments", "about"};
inline const QStringList kPageWritten{"prologue", "epilogue"};
inline const QStringList kPageLead{"cover", "copyright", "dedication", "epigraph", "prologue"};
inline const QStringList kPageTail{"epilogue", "acknowledgments", "about"};
inline const QStringList kPageKinds{"cover", "copyright", "dedication", "epigraph", "prologue", "part",
                                    "epilogue", "acknowledgments", "about"};

QStringList chapterOrder(const QJsonObject &book);
void setChapterOrder(QJsonObject &book, const QStringList &order);

QString chapterKind(const QString &chId, const QJsonObject &book);
bool isStory(const QString &chId, const QJsonObject &book);
// "prologue" / "epilogue" / empty
QString chapterRole(const QString &chId, const QJsonObject &book);
// a chapter's number counts chapters only; a part's, parts only
int kindCount(const QString &chId, const QString &kind, const QJsonObject &book);
inline int chapterNumber(const QString &chId, const QJsonObject &book) { return kindCount(chId, "chapter", book); }
QString chapterTitle(const QString &chId, const QJsonObject &book);
QString chapterName(const QString &chId, const QJsonObject &book);
// the heading as a reader sees it: name, then any title. customTitlesOnly is
// library.exportCustomChapterTitles (File → Export → Chapter Titles Only).
QString chapterHeading(const QString &chId, const QJsonObject &book, bool customTitlesOnly = false,
                       const QString &sep = QStringLiteral(" — "));
QString kindName(const QString &kind);
QString pageKindName(const QString &kind);
// a chapter's number, a part's roman numeral, a fleuron for the rest
QString chapterMark(const QString &chId, const QJsonObject &book);
int numberedChapters(const QJsonObject &book, const QString &chId = {});
// a story of one chapter is "the story": no heading until a second joins it
QString soloStory(const QJsonObject &book);
// NEO 1.0's prologue/epilogue roles become kinds; kinds of vanished entries go.
// Returns true when the book changed.
bool settleChapterKinds(QJsonObject &book);
void setChapterKind(QJsonObject &book, const QString &chId, const QString &kind);

QString roman(int n);
QString partLabel(int n);
bool isUntitled(const QString &s);
bool isScript(const QJsonObject &book);

// a word holds at least one letter or digit; Thai, Lao, Myanmar and Khmer are
// counted by ICU's word boundaries, since they put no spaces between words
int countWords(const QString &text);

} // namespace neosea
