#pragma once
// Bringing manuscripts home: .docx, .txt and .md become books, chapters and
// scene breaks detected; .fountain and .fdx are handed over as they stand for
// the screenplay reader to sort into elements.

#include "core/typing.h"

#include <QList>
#include <QString>

namespace neosea {

struct ImportPara {
    QString text;          // Markdown-ish: **bold**, *italic*
    bool scene = false;    // a *** break
};

struct ImportChapter {
    QString title;
    QList<ImportPara> paras;
    QString role;          // "prologue" / "epilogue" / empty
};

struct ImportResult {
    QString name;          // the file's name without its extension
    QString title, author; // harvested from the document, when it had them
    QList<ImportChapter> chapters;
    QString script;        // "fountain" / "fdx": source holds the file as it stands
    QString source;
    QString error;
};

// One Word paragraph read from document.xml
struct DocxPara {
    QString text;
    bool pageBreak = false, heading = false, title = false;
};

ImportResult importFile(const QString &path);
// The halves, for tests
QList<DocxPara> docxParagraphs(const QString &documentXml, const QString &stylesXml);
ImportResult chapterize(const QString &name, const QList<DocxPara> &paras);
QList<DocxPara> textParagraphs(const QString &text);

// A chapter's HTML from what an import found: dialogue dashes as if typed,
// **bold** and *italic* as tags
QString importedChapterHtml(const ImportChapter &ch, const typing::DashStyle &dashes);

// Reads one file out of a zip (a .docx, an .epub)
QByteArray zipEntry(const QString &zipPath, const QString &entry, bool *ok = nullptr);

} // namespace neosea
