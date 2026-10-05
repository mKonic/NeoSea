#pragma once
// Getting a book out: the book (or one chapter, or a shelf) gathered into
// sections, then built as plain text, Markdown, a web page, Word, EPUB 3
// (KDP-friendly: nav + NCX contents, a cover) or PDF.
//
// Every paragraph is rebuilt from its text runs, so exports carry only what
// the writer meant: text, bold, italic, underline, strikethrough, alignment,
// poetry, flush paragraphs and scene breaks. Ghost notes and placeholder
// marks stop at this door.

#include "core/chapter.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace neosea {

class Library;

struct ExportPara {
    bool sceneBreak = false, poetry = false, flush = false;
    QString text;        // plain, no-break spaces as spaces, trimmed
    QList<Run> runs;     // with text only
    QString align;
};

struct Section {
    QString kind = "chapter"; // chapter, part, opener, copyright, dedication, epigraph, acknowledgments, about
    QString heading, partTitle, label, subtitle, byline, role, chId;
    int level = 0;
    int num = 0;
    bool front = false;
    QList<ExportPara> paras;
};

struct TocEntry {
    QString label;
    int num = 0, level = 0;
    QString type; // part, title, chapter, page
};

struct ExportData {
    QString id, uuid, title, subtitle, author, language, coverSeed, coverImage;
    QList<Section> sections;
    QList<TocEntry> toc;
    bool contents = false;          // a printed contents page
    bool contentsChapters = true;   // it lists the chapters too
    QString chapterOnly;            // one chapter, on its own: its name
};

QList<ExportPara> parasFromHtml(const QString &html);
// a heading as plain text: a part's name with its title
QString plainHeading(const Section &s);
// letters of every script stay, punctuation goes, spaces become dashes
QString safeName(const QString &s);

// The open book, gathered. chapters: chapterId -> html. Gives the book a uuid
// the first time (returns true when it did, so the caller saves book.json).
bool bookExportData(QJsonObject &book, const QHash<QString, QString> &chapters, bool customTitlesOnly,
                    const QString &language, ExportData &out);
// One chapter, on its own, headed as it is in the book
std::optional<ExportData> chapterExportData(const ExportData &book, const QString &chId);
// A shelf read from disk: bound (its cover, pages and numbering) or as an
// anthology (titles only, each title's chapters counted from 1). A bound
// shelf gets a uuid in its binding the first time (the caller saves).
ExportData shelfBookData(const Library &lib, QJsonObject &shelf, const QJsonObject &library, bool bound,
                         const QString &title, const QString &language);

QString buildTxt(const ExportData &d);
QString buildMd(const ExportData &d);

struct HtmlOptions {
    QString coverMime;       // a cover image on its own first page, when set
    QByteArray coverBytes;
    QString fontsCss;        // @font-face rules carried inside the file
    QString bodyFont = "Gelasio";
    QString dropCapFont = "Libre Bodoni"; // empty: no drop cap
    bool stamp = false;      // "N words · exported from neosea on …"
    bool customTitlesOnly = false;
};
QString buildHtml(const ExportData &d, const HtmlOptions &o = {});

// A zip in memory: entries in order (EPUB's mimetype first, stored)
struct ZipEntry {
    QString path;
    QByteArray content;
    bool store = false;
};
QByteArray buildZip(const QList<ZipEntry> &entries);

QByteArray buildDocx(const ExportData &d);

struct CoverImage {
    QByteArray bytes;
    QString mime = "image/jpeg";
    QString ext = "jpg";
};
QByteArray buildEpub(const ExportData &d, const CoverImage &cover);

// The PDF: the book set in pages, page numbers at the foot of the story's
// pages, a contents page with real page numbers, the drop cap raised.
QByteArray buildPdf(const ExportData &d, const HtmlOptions &o, QString *error = nullptr);

} // namespace neosea
