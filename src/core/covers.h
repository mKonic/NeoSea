#pragma once
// Cover art, as NEO makes it: an art layer (a seeded abstract, or the
// writer's own image) and a type layer (the title set in one of seven
// templates, every line sized to run the cover's full width, and the author).
// The type is real text, so a renamed book re-sets its cover for free.
//
// The randomness is NEO's own (FNV-1a hash, mulberry32), so a book shows the
// same cover here as in NEO.

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>

class QPainter;

namespace neosea::covers {

quint32 hash(const QString &s);

struct Rng {
    quint32 a;
    explicit Rng(quint32 seed) : a(seed) {}
    double operator()();
};

// The abstract for a seed, painted at any size (the tile is 208×300)
QImage paintAbstract(const QString &seed, QSize size = QSize(208, 300));
// The name of the style a seed paints (orbs, horizon, …), for tests
QString styleOf(const QString &seed);

struct Line {
    QString text;
    bool small = false, italic = false;
    double size = 0; // tile px
};
struct Ink {
    bool light = true, scrim = false;
};
struct Plan {
    QString templateId;
    QList<Line> lines;
    Ink ink, authorInk;
    bool longAuthor = false;
};
// Everything about a book's cover type, from its metadata and its art
Plan plan(const QJsonObject &meta, const QImage &art);
QList<Line> breakLines(const QString &title, const QString &templateId);

// Draws art and type into rect (the cover's shape is 104×150; anything
// that size or larger, at that ratio). art: the image shown (empty: the
// abstract). With noType, the art alone.
void paint(QPainter &p, const QRectF &rect, const QJsonObject &meta, const QImage &art = {});
// The full cover as a picture: KDP's 1600×2560 unless told otherwise
QImage renderFull(const QJsonObject &meta, const QImage &image = {}, QSize size = QSize(1600, 2560));

} // namespace neosea::covers
