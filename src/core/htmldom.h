#pragma once
// A small, forgiving HTML tree: enough to read NEO's chapter files (and the
// HTML a clipboard hands over from Word, Docs or a browser) and to write
// chapters back the way Chromium's innerHTML does, so a file round-trips
// between NEO and neosea byte for byte where nothing changed.
//
// Nothing here loads or runs anything: scripts and styles are dropped whole.

#include <QList>
#include <QPair>
#include <QString>

#include <memory>

namespace neosea::html {

struct Node {
    enum Type { Element, Text } type = Element;
    QString tag;                              // lower case, elements only
    QList<QPair<QString, QString>> attrs;     // in document order
    QString text;                             // text nodes only, entities decoded
    QList<std::shared_ptr<Node>> children;
    Node *parent = nullptr;

    bool isElement(const char *name = nullptr) const;
    QString attr(const QString &name) const;
    bool hasAttr(const QString &name) const;
    void setAttr(const QString &name, const QString &value);
    void removeAttr(const QString &name);
    QStringList classes() const;
    bool hasClass(const QString &cls) const;
    void addClass(const QString &cls);
    void removeClass(const QString &cls);
    // one CSS property out of the style attribute, lower-cased and trimmed
    QString style(const QString &property) const;

    QString textContent() const;
    // every descendant element, document order
    QList<Node *> descendants(const char *tag = nullptr);
    void appendChild(std::shared_ptr<Node> child);
    // take this node out of its parent, putting its children in its place
    void unwrap();
    void remove();
};
using NodePtr = std::shared_ptr<Node>;

NodePtr element(const QString &tag);
NodePtr textNode(const QString &text);

// Parses a fragment (what innerHTML holds) into a root whose tag is "#root".
NodePtr parse(const QString &html);
// The children of a node, as innerHTML spells them
QString innerHtml(const Node &n);
QString outerHtml(const Node &n);

QString escapeText(const QString &s);   // & < > and no-break space, as innerHTML
QString escapeAttr(const QString &s);   // & " and no-break space
QString escHtml(const QString &s);      // NEO's escHtml: & < > only
QString decodeEntities(const QString &s);

bool isVoid(const QString &tag);
bool isBlock(const QString &tag);

} // namespace neosea::html
