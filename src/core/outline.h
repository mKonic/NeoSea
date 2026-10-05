#pragma once
// The outline lives in the manuscript. A chapter's sections are its pieces
// between *** lines; an outline note for a section (book.sectionNotes) stands
// on the page as a grey ghost paragraph carrying the note's id (data-sec-id)
// until the writer writes over it, and the prose that replaces it keeps the
// id. A chapter's own note is book.chapterNotes; ideas with no home yet are
// book.looseCards. Moving a card moves the writing with it.

#include "core/booksession.h"
#include "core/chapter.h"

#include <QJsonArray>
#include <QList>
#include <QString>

#include <optional>

namespace neosea::outline {

struct Segment {
    int brk = -1;         // the *** opening it (-1: the chapter's first section)
    QList<int> ps;        // its paragraphs
    QString id;           // the note it belongs to, if any
    int words = 0;        // its prose (ghosts don't count)
    QString first;        // its first line of prose
    bool flag = false;    // a placeholder waits in it
};

struct Note {
    QString id, text;
    bool dismissed = false; // the walking note was put away (the card keeps it)
};

QList<Note> notesOf(const QJsonArray &a);
QJsonArray toJson(const QList<Note> &notes);
QString newSectionId();
QString letter(int i); // A, B, C …

// A chapter cut at its *** lines. Only the first piece to hold a note's id is
// that note's (paragraphs split from a written ghost carry the id along).
QList<Segment> segments(const QList<Para> &paras, const QList<Note> &notes);
// a chapter's notes in the order their sections stand on the page (notes with
// no place yet keep to the end)
QList<Note> ordered(const QList<Para> &paras, const QList<Note> &notes);

// Notes into ghosts: a ghost whose note is gone leaves (with the *** made for
// it); the rest take their note's words; a note with no place gets one before
// the next section that's already there, else at the end. Ghosts never move.
void syncGhosts(QList<Para> &paras, const QList<Note> &notes);
// a new ghost before paragraph index `before` (-1: at the end)
void placeGhost(QList<Para> &paras, const Note &sec, int before);
// a ghost leaves, and the *** that set it apart
void removeGhost(QList<Para> &paras, int index);
// the note of the section paragraph i is written in, for the walking note:
// empty on a ghost, a ***, or a section that's still only its ghost
QString sectionIdAt(const QList<Para> &paras, const QList<Note> &notes, int i);
// where the section holding paragraph i begins: its *** or the chapter's first line
int segmentStart(const QList<Para> &paras, int i);

// Book-level moves on an open session (each takes a structural snapshot)
class Board {
public:
    explicit Board(BookSession &s) : m_s(s) {}

    QList<Para> paras(const QString &chId) const;
    void setParas(const QString &chId, const QList<Para> &p);
    QList<Note> notes(const QString &chId) const;
    void setNotes(const QString &chId, const QList<Note> &n);
    QList<Segment> segments(const QString &chId) const;

    // a note on a section's card; a new card's note becomes a ghost after
    // section `after` (-1: before the first section)
    void setChapterNote(const QString &chId, const QString &text);
    QString addSection(const QString &chId, int after, const QString &text);
    void setSectionNote(const QString &chId, const QString &secId, const QString &text);
    // a section written without an outline gets its first note: its first
    // line of prose carries the note's id from then on
    QString noteUnwrittenSection(const QString &chId, int segIdx, const QString &text);
    void deleteSectionNote(const QString &chId, const QString &secId);
    void dismissNote(const QString &chId, const QString &secId);

    // a section moves before section `before` of another chapter (-1: the end)
    void moveSection(const QString &fromCh, int segIdx, const QString &toCh, int before);
    // a note not on the page yet just changes chapters
    void moveVirtualNote(const QString &fromCh, const QString &secId, const QString &toCh);
    void moveChapter(const QString &chId, int index);
    // a section on its own becomes the next chapter, its note the chapter's
    QString sectionToChapter(const QString &chId, int segIdx);
    // an unwritten section's note goes to the loose cards
    bool sectionToLoose(const QString &chId, int segIdx);
    void looseToSection(const QString &looseId, const QString &toCh, int before);
    // one chapter becomes a section of another: its writing to the end of the
    // other after a ***, its note (or its title) that section's note
    std::optional<QString> joinChapter(const QString &chId, const QString &intoCh);
    // the List's ⇧Tab: a section (and those after it) becomes the next chapter
    QString sectionNoteToChapter(const QString &chId, const QString &secId);

    QJsonArray loose() const;
    QString addLoose(const QString &text);
    void setLoose(const QString &id, const QString &text);
    void removeLoose(const QString &id);

    // the first section that isn't the chapter's opening, or -1
    int firstSectionIndex(const QString &chId) const;

private:
    void snap(const QString &label) { m_s.snapshot(label); }
    void repointStickies(const QList<Para> &moved, const QString &toCh);
    BookSession &m_s;
};

} // namespace neosea::outline
