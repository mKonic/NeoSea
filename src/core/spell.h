#pragma once
// The spellcheck pass, as NEO runs it: Hunspell with the dictionaries NEO
// bundles, words found the way NEO finds them (letters of any alphabet with
// their accents, apostrophes, hyphenated words judged whole first), and only
// when the writer asks (Ctrl+;): NEO never nags.

#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace neosea::spell {

struct Piece {
    int start = 0, end = 0;
    QString word;
};
// a run of letters, hyphens and all, and the words in it worth asking about
struct Occurrence {
    int start = 0, end = 0;
    QString whole; // the hyphenated word, when the dictionary should see it whole
    QList<Piece> parts;
};

QString norm(const QString &word); // ’ → ', no quotes at the ends
QList<Occurrence> occurrences(const QString &text);
// the stretches to underline (start, end), given what the dictionary says
QList<QPair<int, int>> wrong(const QList<Occurrence> &found, const std::function<bool(const QString &)> &correct);

struct Language {
    QString code, label;
};
const QList<Language> &languages();
// the interface's own language when there's a dictionary for it (fr-CA → fr), else US English
QString defaultLanguage(const QString &ui);
// where a language's .aff/.dic are: the bundled ones, else the system's; empty when neither
QString dictionaryBase(const QString &resources, const QString &code);

// Hunspell over one dictionary. Loading is safe on another thread; checking
// while it loads says every word is fine rather than crying wolf.
class Speller {
public:
    Speller();
    ~Speller();
    bool load(const QString &base, const QString &code, const QStringList &custom); // base: path without .aff/.dic
    bool loaded() const;
    QString language() const;
    bool check(const QString &word) const;
    QStringList suggest(const QString &word) const; // six at most
    void add(const QString &word);

private:
    struct Engine;
    mutable QMutex m_lock;
    std::unique_ptr<Engine> m_engine;
};

} // namespace neosea::spell
